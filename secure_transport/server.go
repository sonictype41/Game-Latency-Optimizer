package secure

import (
	"bytes"
	"crypto/ecdh"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"net"
	"net/netip"
	"os"
	"sync"
	"time"
)

const HelloSize = 104
const AuthHelloPrefix = 106
const ResponsePrefix = 112
const PendingLifetime = 5 * time.Second

type pendingKey struct {
	peer   netip.AddrPort
	client [64]byte // client ephemeral public key + client random
	ticket [16]byte
}

// Admission is the only information SecureTransport needs from a grant issuer.
// Ticket verification/account policy lives outside this package.
type Admission struct {
	TicketID   [16]byte
	SessionTTL time.Duration
}

type Entry struct {
	channel  *Channel
	peer     netip.AddrPort
	created  time.Time
	deadline time.Time
	active   bool
	hello    pendingKey
	response []byte
}
type bucket struct {
	second int64
	n      int
}

// Limits bounds unauthenticated cookie replies and verified HELLO work. Zero
// disables a rate/cap. These are operational DoS controls, not crypto policy.
type Limits struct {
	RetryGlobalPPS    int
	VerifiedGlobalPPS int
	VerifiedPerIPPPS  int
	MaxInflightGlobal int
	MaxInflightPerIP  int
	MaxRateTrackedIPs int
}

func DefaultLimits() Limits {
	return Limits{
		RetryGlobalPPS:    1000,
		VerifiedGlobalPPS: 100,
		VerifiedPerIPPPS:  20,
		MaxInflightGlobal: 256,
		MaxInflightPerIP:  32,
		MaxRateTrackedIPs: 4096,
	}
}

type Server struct {
	// mu protects only server maps/rate bookkeeping/publication. Expensive X25519,
	// HKDF, core session allocation and WELCOME AEAD are intentionally outside it.
	mu            sync.RWMutex
	key           *ecdh.PrivateKey
	cookie        [32]byte
	entries       map[uint64]*Entry
	pending       map[pendingKey]uint64
	inflight      map[pendingKey]time.Time
	inflightPerIP map[netip.Addr]int
	perPeer       map[netip.Addr]bucket
	allHello      bucket
	allRetry      bucket
	limits        Limits
}

func LoadKey(path string) (*Server, error) {
	info, e := os.Lstat(path)
	if e != nil {
		return nil, e
	}
	if !info.Mode().IsRegular() || info.Mode().Perm()&0077 != 0 {
		return nil, errors.New("relay key must be a regular file with permissions 0600")
	}
	b, e := os.ReadFile(path)
	if e != nil {
		return nil, e
	}
	defer clear(b)
	trimmed := bytes.TrimSpace(b)
	if len(trimmed) != 64 {
		return nil, errors.New("relay key must contain 64 hexadecimal characters")
	}
	var raw [32]byte
	n, e := hex.Decode(raw[:], trimmed)
	if e != nil || n != len(raw) {
		clear(raw[:])
		return nil, errors.New("relay key must contain 64 hexadecimal characters")
	}
	defer clear(raw[:])
	key, e := ecdh.X25519().NewPrivateKey(raw[:])
	if e != nil {
		return nil, e
	}
	return NewServer(key)
}

func GenerateKey(path string) (string, error) {
	k, e := ecdh.X25519().GenerateKey(rand.Reader)
	if e != nil {
		return "", e
	}
	f, e := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0600)
	if e != nil {
		return "", e
	}
	priv := k.Bytes()
	defer clear(priv)
	var text [65]byte
	hex.Encode(text[:64], priv)
	text[64] = '\n'
	_, e = f.Write(text[:])
	clear(text[:])
	ce := f.Close()
	if e != nil {
		return "", e
	}
	if ce != nil {
		return "", ce
	}
	return hex.EncodeToString(k.PublicKey().Bytes()), nil
}

func NewServer(key *ecdh.PrivateKey) (*Server, error) {
	s := &Server{
		key:           key,
		entries:       make(map[uint64]*Entry),
		pending:       make(map[pendingKey]uint64),
		inflight:      make(map[pendingKey]time.Time),
		inflightPerIP: make(map[netip.Addr]int),
		perPeer:       make(map[netip.Addr]bucket),
		limits:        DefaultLimits(),
	}
	if _, e := rand.Read(s.cookie[:]); e != nil {
		return nil, e
	}
	return s, nil
}

func take(b *bucket, now time.Time, max int) bool {
	if max <= 0 {
		return true
	}
	if b.second != now.Unix() {
		b.second = now.Unix()
		b.n = 0
	}
	if b.n >= max {
		return false
	}
	b.n++
	return true
}

func (s *Server) SetLimits(l Limits) {
	s.mu.Lock()
	s.limits = l
	s.mu.Unlock()
}

func (s *Server) PublicKey() []byte { return append([]byte(nil), s.key.PublicKey().Bytes()...) }

func (s *Server) Limits() Limits {
	s.mu.RLock()
	l := s.limits
	s.mu.RUnlock()
	return l
}

func (s *Server) releaseInflightLocked(k pendingKey) {
	if _, ok := s.inflight[k]; !ok {
		return
	}
	delete(s.inflight, k)
	ip := k.peer.Addr()
	if s.inflightPerIP[ip] > 1 {
		s.inflightPerIP[ip]--
	} else {
		delete(s.inflightPerIP, ip)
	}
}

func addrPort(peer *net.UDPAddr) (netip.AddrPort, bool) {
	if peer == nil {
		return netip.AddrPort{}, false
	}
	ap := peer.AddrPort()
	if !ap.IsValid() {
		return netip.AddrPort{}, false
	}
	return netip.AddrPortFrom(ap.Addr().Unmap(), ap.Port()), true
}

func (s *Server) cookieFor(peer netip.AddrPort, hello []byte, slot int64) [32]byte {
	h := hmac.New(sha256.New, s.cookie[:])
	_, _ = h.Write([]byte("GLO6 cookie\x00"))
	a := peer.Addr().AsSlice()
	_, _ = h.Write(a)
	var p [2]byte
	binary.BigEndian.PutUint16(p[:], peer.Port())
	_, _ = h.Write(p[:])
	_, _ = h.Write(hello[8:72])
	var n [8]byte
	binary.BigEndian.PutUint64(n[:], uint64(slot))
	_, _ = h.Write(n[:])
	var out [32]byte
	h.Sum(out[:0])
	return out
}

func makePendingKey(peer netip.AddrPort, hello []byte, ticketID [16]byte) pendingKey {
	var k pendingKey
	k.peer = peer
	copy(k.client[:], hello[8:72])
	k.ticket = ticketID
	return k
}

// Handshake implements GLO signed-grant admission: a cheap cookie challenge is
// completed before the relay asks the issuer verifier to authenticate a
// short-lived single-use bearer grant. The grant is claimed only after a
// session is successfully allocated.
//
// Wire format:
//
//	HELLO      = legacy 104-byte GLH6 type 1 (no ticket)
//	RETRY      = 104-byte GLH6 type 2
//	AUTH_HELLO = 104-byte prefix with type 4 + uint16 ticket length + ticket
func (s *Server) Handshake(peer *net.UDPAddr, b []byte, now time.Time,
	authorize func(ticket []byte, now time.Time) (Admission, bool),
	allocate func(Admission) (uint64, bool), redeem func(ticket, identity []byte, now time.Time) bool,
	welcome func(uint64, Admission) []byte, accepted func(uint64, Admission), discard func(uint64)) []byte {
	if len(b) < HelloSize || string(b[:4]) != "GLH6" || (b[4] != 1 && b[4] != 4) || b[5] != 0 || b[6] != 0 || b[7] != 0 {
		return nil
	}
	if b[4] == 1 && len(b) != HelloSize {
		return nil
	}
	if b[4] == 4 {
		if len(b) < AuthHelloPrefix {
			return nil
		}
		n := int(binary.BigEndian.Uint16(b[104:106]))
		if n == 0 || len(b) != AuthHelloPrefix+n {
			return nil
		}
	}
	ap, ok := addrPort(peer)
	if !ok {
		return nil
	}
	slot := now.Unix() / 30
	current := s.cookieFor(ap, b[:HelloSize], slot)
	previous := s.cookieFor(ap, b[:HelloSize], slot-1)
	if !hmac.Equal(b[72:104], current[:]) && !hmac.Equal(b[72:104], previous[:]) {
		s.mu.Lock()
		allowed := take(&s.allRetry, now, s.limits.RetryGlobalPPS)
		s.mu.Unlock()
		if !allowed {
			return nil
		}
		out := append([]byte{}, b[:HelloSize]...)
		out[4] = 2
		copy(out[72:104], current[:])
		return out
	}
	// A cookie-valid type-1 HELLO is not admission. admission requires a ticket.
	if b[4] != 4 || authorize == nil || allocate == nil {
		return nil
	}
	ticket := b[AuthHelloPrefix:]
	admission, ok := authorize(ticket, now)
	if !ok || admission.SessionTTL <= 0 {
		return nil
	}
	hk := makePendingKey(ap, b, admission.TicketID)

	s.mu.Lock()
	if id := s.pending[hk]; id != 0 {
		if e := s.entries[id]; e != nil && now.Sub(e.created) < PendingLifetime {
			out := append([]byte{}, e.response...)
			s.mu.Unlock()
			return out
		}
	}
	if t, exists := s.inflight[hk]; exists && now.Sub(t) < PendingLifetime {
		s.mu.Unlock()
		return nil
	}
	ip := ap.Addr()
	if s.limits.MaxInflightGlobal > 0 && len(s.inflight) >= s.limits.MaxInflightGlobal {
		s.mu.Unlock()
		return nil
	}
	if s.limits.MaxInflightPerIP > 0 && s.inflightPerIP[ip] >= s.limits.MaxInflightPerIP {
		s.mu.Unlock()
		return nil
	}
	lim, tracked := s.perPeer[ip]
	if !tracked && s.limits.MaxRateTrackedIPs > 0 && len(s.perPeer) >= s.limits.MaxRateTrackedIPs {
		s.mu.Unlock()
		return nil
	}
	if !take(&lim, now, s.limits.VerifiedPerIPPPS) {
		s.mu.Unlock()
		return nil
	}
	if !take(&s.allHello, now, s.limits.VerifiedGlobalPPS) {
		s.mu.Unlock()
		return nil
	}
	s.perPeer[ip] = lim
	s.inflight[hk] = now
	s.inflightPerIP[ip]++
	s.mu.Unlock()

	published := false
	defer func() {
		if !published {
			s.mu.Lock()
			s.releaseInflightLocked(hk)
			s.mu.Unlock()
		}
	}()
	eph, e := ecdh.X25519().GenerateKey(rand.Reader)
	if e != nil {
		return nil
	}
	a, e := DH(s.key, b[8:40])
	if e != nil {
		return nil
	}
	defer clear(a)
	z, e := DH(eph, b[8:40])
	if e != nil {
		return nil
	}
	defer clear(z)
	sid, ok := allocate(admission)
	if !ok {
		return nil
	}
	rollback := true
	defer func() {
		if rollback && discard != nil {
			discard(sid)
		}
	}()
	c2s, s2c, e := Derive(a, z, b[8:40], b[40:72], s.key.PublicKey().Bytes(), eph.PublicKey().Bytes(), sid)
	if e != nil {
		return nil
	}
	defer clear(c2s)
	defer clear(s2c)
	deadline := now.Add(admission.SessionTTL)
	ch, e := NewChannel(sid, s2c, c2s, deadline)
	if e != nil {
		return nil
	}
	var welcomePlain []byte
	if welcome != nil {
		welcomePlain = welcome(sid, admission)
	}
	enc, e := ch.Seal(welcomePlain)
	if e != nil {
		return nil
	}
	out := make([]byte, ResponsePrefix, ResponsePrefix+len(enc))
	copy(out, "GLH6")
	out[4] = 3
	copy(out[8:72], b[8:72])
	copy(out[72:104], eph.PublicKey().Bytes())
	binary.BigEndian.PutUint64(out[104:112], sid)
	out = append(out, enc...)
	if redeem == nil {
		return nil
	}
	identity := append([]byte(ap.String()+"\x00"), b[8:72]...)
	if !redeem(ticket, identity, now) {
		return nil
	}
	s.mu.Lock()
	s.releaseInflightLocked(hk)
	s.entries[sid] = &Entry{channel: ch, peer: ap, created: now, deadline: deadline, hello: hk, response: out}
	s.pending[hk] = sid
	s.mu.Unlock()
	published = true
	rollback = false
	if accepted != nil {
		accepted(sid, admission)
	}
	return out
}

// Open authenticates/decrypts a record in-place. Returned plaintext aliases b.
func (s *Server) Open(peer *net.UDPAddr, b []byte, now time.Time) (uint64, []byte, bool) {
	if len(b) < HeaderSize+16 || len(b) > MaxDatagram || string(b[:4]) != "GLO6" {
		return 0, nil, false
	}
	ap, ok := addrPort(peer)
	if !ok {
		return 0, nil, false
	}
	id := binary.BigEndian.Uint64(b[8:16])
	s.mu.RLock()
	e := s.entries[id]
	valid := e != nil && e.peer == ap && now.Before(e.deadline) && (e.active || now.Sub(e.created) < PendingLifetime)
	s.mu.RUnlock()
	if !valid {
		return 0, nil, false
	}
	p, err := e.channel.OpenInPlace(b)
	return id, p, err == nil
}

func (s *Server) Active(id uint64) bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	e := s.entries[id]
	return e != nil && e.active
}
func (s *Server) Activate(id uint64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if e := s.entries[id]; e != nil {
		e.active = true
	}
}
func (s *Server) Seal(id uint64, p []byte) ([]byte, error) { return s.SealInto(id, nil, p) }
func (s *Server) SealInto(id uint64, dst, p []byte) ([]byte, error) {
	s.mu.RLock()
	e := s.entries[id]
	s.mu.RUnlock()
	if e == nil {
		return nil, ErrInvalid
	}
	return e.channel.SealInto(dst, p)
}
func (s *Server) Remove(id uint64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if e := s.entries[id]; e != nil {
		delete(s.pending, e.hello)
		delete(s.entries, id)
	}
}

// Cleanup releases secure state and returns core sessions that must be closed.
// Call without holding the core lock; no callback is made while holding s.mu.
func (s *Server) Cleanup(now time.Time, alive func(uint64) bool) []uint64 {
	s.mu.RLock()
	ids := make([]uint64, 0, len(s.entries))
	for id := range s.entries {
		ids = append(ids, id)
	}
	s.mu.RUnlock()
	var expired []uint64
	for _, id := range ids {
		live := alive(id)
		s.mu.Lock()
		e := s.entries[id]
		if e != nil && (!live || !now.Before(e.deadline) || (!e.active && now.Sub(e.created) >= PendingLifetime)) {
			delete(s.pending, e.hello)
			delete(s.entries, id)
			expired = append(expired, id)
		}
		s.mu.Unlock()
	}
	s.mu.Lock()
	for ip, b := range s.perPeer {
		if now.Unix()-b.second > 60 {
			delete(s.perPeer, ip)
		}
	}
	for k, t := range s.inflight {
		if now.Sub(t) >= PendingLifetime {
			s.releaseInflightLocked(k)
		}
	}
	s.mu.Unlock()
	return expired
}
