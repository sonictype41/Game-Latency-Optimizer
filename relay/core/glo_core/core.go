package glocore

import (
	"crypto/rand"
	"encoding/binary"
	"errors"
	"math/bits"
	"net"
	"net/netip"
	"sync"
	"sync/atomic"
	"time"
)

const (
	HeaderSize         = 32
	MaxPayload         = 1400
	MaxDatagram        = HeaderSize + MaxPayload
	MaxInnerUDPPayload = 1372 // 1400-byte inner IPv4 MTU minus IPv4+UDP headers
	DefaultPort        = 43170
	Version            = 2
)

var Magic = [4]byte{'G', 'L', 'O', '2'}

const FinishFlagGLOD1 uint16 = 0x0001

type PacketType uint8

const (
	Hello         PacketType = 1
	Welcome       PacketType = 2
	Ping          PacketType = 3
	Pong          PacketType = 4
	Bye           PacketType = 5
	Error         PacketType = 6
	FlowClose     PacketType = 11
	StatsRequest  PacketType = 14
	StatsResponse PacketType = 15
	Finish        PacketType = 16
	FinishAck     PacketType = 17
)

type RelayRejectReason uint16

const (
	RejectNone        RelayRejectReason = 0
	RejectCapacity    RelayRejectReason = 1
	RejectMaintenance RelayRejectReason = 2
	RejectTemporary   RelayRejectReason = 3
)

type FlowAdmission uint8

const (
	FlowAccepted FlowAdmission = iota
	FlowRejectedCapacity
	FlowRejectedMaintenance
	FlowRejectedTemporary
)

type Packet struct {
	Type      PacketType
	Flags     uint16
	SessionID uint64
	Nonce     uint64
	FlowID    uint32
	Payload   []byte
}

func validType(t PacketType) bool {
	switch t {
	case Hello, Welcome, Ping, Pong, Bye, Error, FlowClose, StatsRequest, StatsResponse, Finish, FinishAck:
		return true
	default:
		return false
	}
}

func Encode(p Packet) ([]byte, error) {
	if !validType(p.Type) || len(p.Payload) > MaxPayload {
		return nil, errors.New("invalid packet")
	}
	b := make([]byte, HeaderSize+len(p.Payload))
	copy(b[0:4], Magic[:])
	b[4] = Version
	b[5] = byte(p.Type)
	binary.BigEndian.PutUint16(b[6:8], p.Flags)
	binary.BigEndian.PutUint64(b[8:16], p.SessionID)
	binary.BigEndian.PutUint64(b[16:24], p.Nonce)
	binary.BigEndian.PutUint32(b[24:28], p.FlowID)
	binary.BigEndian.PutUint16(b[28:30], uint16(len(p.Payload)))
	copy(b[HeaderSize:], p.Payload)
	return b, nil
}

func decodePacket(b []byte, copyPayload bool) (Packet, error) {
	if len(b) < HeaderSize || len(b) > MaxDatagram {
		return Packet{}, errors.New("bad packet size")
	}
	if b[0] != Magic[0] || b[1] != Magic[1] || b[2] != Magic[2] || b[3] != Magic[3] || b[4] != Version {
		return Packet{}, errors.New("bad magic/version")
	}
	if b[30] != 0 || b[31] != 0 {
		return Packet{}, errors.New("reserved bits set")
	}
	t := PacketType(b[5])
	if !validType(t) {
		return Packet{}, errors.New("bad type")
	}
	plen := int(binary.BigEndian.Uint16(b[28:30]))
	if plen > MaxPayload || len(b) != HeaderSize+plen {
		return Packet{}, errors.New("bad payload size")
	}
	payload := b[HeaderSize:]
	if copyPayload && plen != 0 {
		owned := make([]byte, plen)
		copy(owned, payload)
		payload = owned
	}
	return Packet{
		Type:      t,
		Flags:     binary.BigEndian.Uint16(b[6:8]),
		SessionID: binary.BigEndian.Uint64(b[8:16]),
		Nonce:     binary.BigEndian.Uint64(b[16:24]),
		FlowID:    binary.BigEndian.Uint32(b[24:28]),
		Payload:   payload,
	}, nil
}

// Decode returns an owned payload, preserving the historical API for callers
// that retain Packet beyond the lifetime of the input datagram.
func Decode(b []byte) (Packet, error) { return decodePacket(b, true) }

// DecodeView validates and decodes a packet without copying its payload.
// Payload aliases b and is valid only until the caller reuses/modifies b.
// The relay receive loop uses this on the synchronous hot DATA path.
func DecodeView(b []byte) (Packet, error) { return decodePacket(b, false) }

func EncodeFlowEndpoint(ip net.IP, targetPort, clientPort uint16) ([]byte, error) {
	ip4 := ip.To4()
	if ip4 == nil {
		return nil, errors.New("IPv4 required")
	}
	b := make([]byte, 8)
	copy(b[:4], ip4)
	binary.BigEndian.PutUint16(b[4:6], targetPort)
	binary.BigEndian.PutUint16(b[6:8], clientPort)
	return b, nil
}

func DecodeFlowEndpoint(b []byte) (net.IP, uint16, uint16, error) {
	if len(b) != 8 {
		return nil, 0, 0, errors.New("bad flow endpoint")
	}
	ip := net.IPv4(b[0], b[1], b[2], b[3]).To4()
	return ip, binary.BigEndian.Uint16(b[4:6]), binary.BigEndian.Uint16(b[6:8]), nil
}

type Flow struct {
	ID         uint32
	Target     *net.UDPAddr
	ClientPort uint16
	Conn       *net.UDPConn
	LastSeen   time.Time
	// Endpoint caches the untrusted 8-byte route tuple carried by GLOD C2S.
	// It is stored only after userspace TargetPolicy validation and enables the relay
	// hot path to skip IP parsing + CIDR lookup for matching packets.
	Endpoint [8]byte
}

func (f *Flow) SetEndpoint(prefix []byte) bool {
	if f == nil || len(prefix) != len(f.Endpoint) {
		return false
	}
	copy(f.Endpoint[:], prefix)
	return true
}
func (f *Flow) EndpointMatches(prefix []byte) bool {
	if f == nil || len(prefix) != len(f.Endpoint) {
		return false
	}
	var v [8]byte
	copy(v[:], prefix)
	return v == f.Endpoint
}

const (
	LegacyDataCountersSize = 32
	DataCountersSize       = 48
	QualityReportVersion   = 1
	QualityReportSize      = 24
	sequenceReorderWindow  = 64
)

type DataCounters struct {
	ClientToRelay  uint64
	RelayToGame    uint64
	GameToRelay    uint64
	RelayToClient  uint64
	C2RSeqReceived uint64
	C2RSeqLost     uint64
}

func EncodeDataCounters(c DataCounters) []byte {
	b := make([]byte, DataCountersSize)
	binary.BigEndian.PutUint64(b[0:8], c.ClientToRelay)
	binary.BigEndian.PutUint64(b[8:16], c.RelayToGame)
	binary.BigEndian.PutUint64(b[16:24], c.GameToRelay)
	binary.BigEndian.PutUint64(b[24:32], c.RelayToClient)
	binary.BigEndian.PutUint64(b[32:40], c.C2RSeqReceived)
	binary.BigEndian.PutUint64(b[40:48], c.C2RSeqLost)
	return b
}

func DecodeDataCounters(b []byte) (DataCounters, error) {
	if len(b) != LegacyDataCountersSize && len(b) != DataCountersSize {
		return DataCounters{}, errors.New("bad stats payload")
	}
	out := DataCounters{
		ClientToRelay: binary.BigEndian.Uint64(b[0:8]),
		RelayToGame:   binary.BigEndian.Uint64(b[8:16]),
		GameToRelay:   binary.BigEndian.Uint64(b[16:24]),
		RelayToClient: binary.BigEndian.Uint64(b[24:32]),
	}
	if len(b) == DataCountersSize {
		out.C2RSeqReceived = binary.BigEndian.Uint64(b[32:40])
		out.C2RSeqLost = binary.BigEndian.Uint64(b[40:48])
	}
	return out, nil
}

type QualityReport struct {
	RelayRTTMicros uint32
	S2CSeqReceived uint64
	S2CSeqLost     uint64
}

func EncodeQualityReport(q QualityReport) []byte {
	b := make([]byte, QualityReportSize)
	b[0] = QualityReportVersion
	binary.BigEndian.PutUint32(b[4:8], q.RelayRTTMicros)
	binary.BigEndian.PutUint64(b[8:16], q.S2CSeqReceived)
	binary.BigEndian.PutUint64(b[16:24], q.S2CSeqLost)
	return b
}

func DecodeQualityReport(b []byte) (QualityReport, error) {
	if len(b) != QualityReportSize || b[0] != QualityReportVersion || b[1] != 0 || b[2] != 0 || b[3] != 0 {
		return QualityReport{}, errors.New("bad quality report")
	}
	return QualityReport{
		RelayRTTMicros: binary.BigEndian.Uint32(b[4:8]),
		S2CSeqReceived: binary.BigEndian.Uint64(b[8:16]),
		S2CSeqLost:     binary.BigEndian.Uint64(b[16:24]),
	}, nil
}

type sequenceLossTracker struct {
	initialized       bool
	base              uint64
	seen              uint64
	finalizedReceived uint64
	finalizedLost     uint64
}

func (t *sequenceLossTracker) finalizePrefix(count uint64) {
	if count == 0 {
		return
	}
	if count >= sequenceReorderWindow {
		present := uint64(bits.OnesCount64(t.seen))
		t.finalizedReceived += present
		t.finalizedLost += sequenceReorderWindow - present
		t.finalizedLost += count - sequenceReorderWindow
		t.seen = 0
		t.base += count
		return
	}
	mask := (uint64(1) << count) - 1
	present := uint64(bits.OnesCount64(t.seen & mask))
	t.finalizedReceived += present
	t.finalizedLost += count - present
	t.seen >>= count
	t.base += count
}

func (t *sequenceLossTracker) observe(sequence uint64) {
	if sequence == 0 {
		return
	}
	if !t.initialized {
		t.initialized = true
		t.base = sequence
	}
	if sequence < t.base {
		return
	}
	delta := sequence - t.base
	if delta >= sequenceReorderWindow {
		t.finalizePrefix(delta - sequenceReorderWindow + 1)
	}
	offset := sequence - t.base
	if offset >= sequenceReorderWindow {
		return
	}
	bit := uint64(1) << offset
	if t.seen&bit != 0 {
		return
	}
	t.seen |= bit
	for t.seen&1 != 0 {
		t.finalizedReceived++
		t.seen >>= 1
		t.base++
	}
}

type tokenBucket struct {
	tokens float64
	last   time.Time
}

func (b *tokenBucket) allow(now time.Time, ratePerSecond, capacity, cost float64) bool {
	if ratePerSecond <= 0 || cost <= 0 {
		return true
	}
	if capacity < cost {
		capacity = cost
	}
	if b.last.IsZero() {
		b.last = now
		b.tokens = capacity
	} else {
		elapsed := now.Sub(b.last).Seconds()
		if elapsed > 0 {
			b.tokens += elapsed * ratePerSecond
			if b.tokens > capacity {
				b.tokens = capacity
			}
			b.last = now
		}
	}
	if b.tokens+1e-9 < cost {
		return false
	}
	b.tokens -= cost
	return true
}

type Session struct {
	mu           sync.Mutex
	closed       bool
	Active       bool
	ID           uint64
	Addr         *net.UDPAddr
	Created      time.Time
	Deadline     time.Time
	TicketID     string
	LastSeen     time.Time
	Flows        map[uint32]*Flow
	PacketRate   tokenBucket
	ByteRate     tokenBucket
	FlowOpenRate tokenBucket
	Counters     DataCounters
	C2RSequence  sequenceLossTracker
	NextS2CSeq   uint64
}

type ServerStats struct {
	ControlSessions        int
	GameplaySessions       int
	ActiveFlows            int
	SessionsCreated        uint64
	SessionsClosed         uint64
	SessionsExpired        uint64
	FlowsOpened            uint64
	FlowsClosed            uint64
	RateLimitedPackets     uint64
	RateLimitedBytes       uint64
	SessionAdmissionDrops  uint64
	FlowAdmissionRateDrops uint64
}

type ipRate struct {
	sec   int64
	count uint32
}

type Server struct {
	// mu protects session-map membership and HELLO/IP admission bookkeeping only.
	// Established packet state is protected by Session.mu so unrelated sessions
	// can run concurrently instead of serializing on one global core mutex.
	mu           sync.RWMutex
	sessions     map[uint64]*Session
	perIP        map[netip.Addr]*ipRate
	controlPerIP map[netip.Addr]int
	allocGlobal  ipRate

	// admissionMu serializes infrequent flow/session lifecycle transitions that
	// affect node-wide gameplay capacity. It is never taken on DATA hot paths.
	admissionMu sync.Mutex

	MaxPPSPerIP             uint32 // deprecated alias for MaxAllocPerIPPPS
	MaxAllocGlobalPPS       uint32
	MaxAllocPerIPPPS        uint32
	MaxSessionPPS           uint32
	MaxSessionKbps          uint64
	SessionBurst            time.Duration
	MaxControlSessions      int
	MaxControlSessionsPerIP int
	MaxGameplaySessions     int
	MaxFlowsPerSess         int
	FlowTimeout             time.Duration
	SessionIdleTimeout      time.Duration
	FlowOpenGlobalPPS       uint32
	FlowOpenPerIPPPS        uint32
	FlowOpenPerSessionPPS   uint32
	flowGlobal              tokenBucket
	flowPerIP               map[netip.Addr]*tokenBucket
	TargetPolicy            TargetPolicy

	gameplaySessions       atomic.Int64
	activeFlows            atomic.Int64
	sessionsCreated        atomic.Uint64
	sessionsClosed         atomic.Uint64
	sessionsExpired        atomic.Uint64
	flowsOpened            atomic.Uint64
	flowsClosed            atomic.Uint64
	rateLimitedPackets     atomic.Uint64
	rateLimitedBytes       atomic.Uint64
	sessionAdmissionDrops  atomic.Uint64
	flowAdmissionRateDrops atomic.Uint64
}

func NewServer() *Server {
	return &Server{
		sessions:                make(map[uint64]*Session),
		perIP:                   make(map[netip.Addr]*ipRate),
		controlPerIP:            make(map[netip.Addr]int),
		flowPerIP:               make(map[netip.Addr]*tokenBucket),
		MaxPPSPerIP:             2000,
		MaxAllocGlobalPPS:       0,
		MaxAllocPerIPPPS:        0,
		MaxSessionPPS:           1000,
		MaxSessionKbps:          2048,
		SessionBurst:            250 * time.Millisecond,
		MaxControlSessions:      4096,
		MaxControlSessionsPerIP: 512,
		MaxGameplaySessions:     200,
		MaxFlowsPerSess:         8,
		FlowTimeout:             15 * time.Second,
		SessionIdleTimeout:      25 * time.Second,
		FlowOpenGlobalPPS:       2000,
		FlowOpenPerIPPPS:        500,
		FlowOpenPerSessionPPS:   50,
	}
}

func randomSessionID() uint64 {
	var b [8]byte
	if _, err := rand.Read(b[:]); err != nil {
		return 0
	}
	id := binary.BigEndian.Uint64(b[:])
	if id == 0 {
		id = 1
	}
	return id
}

func endpointAddr(addr *net.UDPAddr) netip.Addr {
	if addr == nil {
		return netip.Addr{}
	}
	ap := addr.AddrPort()
	if !ap.IsValid() {
		return netip.Addr{}
	}
	return ap.Addr().Unmap()
}

// fixedWindowAllow is used only on low-frequency admission paths.
func fixedWindowAllow(r *ipRate, now time.Time, max uint32) bool {
	if max == 0 {
		return true
	}
	sec := now.Unix()
	if r.sec != sec {
		r.sec = sec
		r.count = 0
	}
	if r.count >= max {
		return false
	}
	r.count++
	return true
}

// allowIPLocked gates only new control-session allocation. Established
// sessions never touch this map. Caller holds s.mu.
func (s *Server) allowIPLocked(addr *net.UDPAddr, now time.Time) bool {
	key := endpointAddr(addr)
	if !key.IsValid() {
		return false
	}
	perIPMax := s.MaxAllocPerIPPPS
	if perIPMax == 0 {
		perIPMax = s.MaxPPSPerIP // backward-compatible programmatic alias
	}
	if !fixedWindowAllow(&s.allocGlobal, now, s.MaxAllocGlobalPPS) {
		return false
	}
	r := s.perIP[key]
	if r == nil {
		r = &ipRate{}
		s.perIP[key] = r
	}
	return fixedWindowAllow(r, now, perIPMax)
}

func sameEndpoint(a, b *net.UDPAddr) bool {
	return a != nil && b != nil && a.Port == b.Port && a.IP.Equal(b.IP)
}

func (s *Server) getSession(id uint64) *Session {
	s.mu.RLock()
	sess := s.sessions[id]
	s.mu.RUnlock()
	return sess
}

func (s *Server) NewSession(addr *net.UDPAddr, now time.Time) (uint64, bool) {
	return s.NewSessionWithGrant(addr, now, "", 0)
}

// NewSessionWithGrant creates a control session whose lifetime is carried by
// the redeemed session ticket. ttl<=0 is reserved for local tests/tools and
// means no business-time expiry; crypto sequence exhaustion still applies.
func (s *Server) NewSessionWithGrant(addr *net.UDPAddr, now time.Time, ticketID string, ttl time.Duration) (uint64, bool) {
	key := endpointAddr(addr)
	if !key.IsValid() {
		s.sessionAdmissionDrops.Add(1)
		return 0, false
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	// Capacity checks precede token consumption so a source that is already at
	// its concurrency cap cannot drain shared allocation budget.
	if (s.MaxControlSessions > 0 && len(s.sessions) >= s.MaxControlSessions) ||
		(s.MaxControlSessionsPerIP > 0 && s.controlPerIP[key] >= s.MaxControlSessionsPerIP) {
		s.sessionAdmissionDrops.Add(1)
		return 0, false
	}
	if !s.allowIPLocked(addr, now) {
		s.sessionAdmissionDrops.Add(1)
		return 0, false
	}
	id := randomSessionID()
	if id == 0 {
		return 0, false
	}
	for s.sessions[id] != nil {
		id = randomSessionID()
		if id == 0 {
			return 0, false
		}
	}
	copyAddr := *addr
	sess := &Session{ID: id, Addr: &copyAddr, Created: now, TicketID: ticketID, LastSeen: now, Flows: make(map[uint32]*Flow)}
	if ttl > 0 {
		sess.Deadline = now.Add(ttl)
	}
	s.sessions[id] = sess
	s.controlPerIP[key]++
	s.sessionsCreated.Add(1)
	return id, true
}

// validateSessionLocked requires sess.mu. It never acquires Server.mu.
func (s *Server) validateSessionLocked(sess *Session, addr *net.UDPAddr, now time.Time, packetBytes int) bool {
	if sess == nil || sess.closed || !sameEndpoint(sess.Addr, addr) {
		return false
	}
	if !sess.Deadline.IsZero() && !now.Before(sess.Deadline) {
		return false
	}
	burstSeconds := s.SessionBurst.Seconds()
	if burstSeconds <= 0 {
		burstSeconds = 0.25
	}
	if s.MaxSessionPPS > 0 {
		rate := float64(s.MaxSessionPPS)
		capacity := rate * burstSeconds
		if capacity < 2 {
			capacity = 2
		}
		if !sess.PacketRate.allow(now, rate, capacity, 1) {
			s.rateLimitedPackets.Add(1)
			return false
		}
	}
	if s.MaxSessionKbps > 0 && packetBytes > 0 {
		rateBytes := float64(s.MaxSessionKbps) * 1000.0 / 8.0
		if !sess.ByteRate.allow(now, rateBytes, rateBytes*burstSeconds, float64(packetBytes)) {
			s.rateLimitedBytes.Add(uint64(packetBytes))
			return false
		}
	}
	sess.LastSeen = now
	return true
}

func (s *Server) ValidateSession(addr *net.UDPAddr, id uint64, now time.Time, packetBytes int) (*Session, bool) {
	sess := s.getSession(id)
	if sess == nil {
		return nil, false
	}
	sess.mu.Lock()
	ok := s.validateSessionLocked(sess, addr, now, packetBytes)
	sess.mu.Unlock()
	if !ok {
		return nil, false
	}
	return sess, true
}

// ActivateSession marks the authenticated control session ready for GLOD.
// It is called only after the encrypted FINISH exchange validates the peer.
func (s *Server) ActivateSession(addr *net.UDPAddr, id uint64, now time.Time) bool {
	sess := s.getSession(id)
	if sess == nil {
		return false
	}
	sess.mu.Lock()
	defer sess.mu.Unlock()
	if sess.closed || !sameEndpoint(sess.Addr, addr) || (!sess.Deadline.IsZero() && !now.Before(sess.Deadline)) {
		return false
	}
	sess.Active = true
	sess.LastSeen = now
	return true
}

// ValidateDataC2S performs established DATA accounting entirely under the
// target session's lock. Different users therefore progress concurrently.
func (s *Server) ValidateDataC2S(addr *net.UDPAddr, id uint64, now time.Time, packetBytes int, sequence uint64, flowID uint32) (*Flow, bool) {
	sess := s.getSession(id)
	if sess == nil {
		return nil, false
	}
	sess.mu.Lock()
	defer sess.mu.Unlock()
	if !sess.Active || !s.validateSessionLocked(sess, addr, now, packetBytes) {
		return nil, false
	}
	sess.Counters.ClientToRelay++
	sess.C2RSequence.observe(sequence)
	flow := sess.Flows[flowID]
	if flow != nil {
		flow.LastSeen = now
	}
	return flow, true
}

// PrepareS2C validates the flow generation, refreshes reverse-only activity,
// accounts game->relay traffic and reserves the next S2C sequence atomically
// within one session. This fixes active reverse-only flows timing out as idle.
func (s *Server) PrepareS2C(sessionID uint64, flowID uint32, flow *Flow, now time.Time) (*net.UDPAddr, uint64, bool) {
	sess := s.getSession(sessionID)
	if sess == nil {
		return nil, 0, false
	}
	sess.mu.Lock()
	defer sess.mu.Unlock()
	if sess.closed || !sess.Active || (!sess.Deadline.IsZero() && !now.Before(sess.Deadline)) || flow == nil || sess.Flows[flowID] != flow {
		return nil, 0, false
	}
	sess.LastSeen = now
	flow.LastSeen = now
	sess.Counters.GameToRelay++
	sess.NextS2CSeq++
	if sess.NextS2CSeq == 0 {
		sess.NextS2CSeq++
	}
	cp := *sess.Addr
	return &cp, sess.NextS2CSeq, true
}

func (s *Server) SessionEndpoint(id uint64) *net.UDPAddr {
	sess := s.getSession(id)
	if sess == nil {
		return nil
	}
	sess.mu.Lock()
	defer sess.mu.Unlock()
	if sess.closed {
		return nil
	}
	cp := *sess.Addr
	return &cp
}

// DataSessionEndpoint returns the peer only while the plaintext data plane is
// active and within the signed-grant session TTL. Shapers use this to avoid
// draining queued gameplay after expiry.
func (s *Server) DataSessionEndpoint(id uint64, now time.Time) *net.UDPAddr {
	sess := s.getSession(id)
	if sess == nil {
		return nil
	}
	sess.mu.Lock()
	defer sess.mu.Unlock()
	if sess.closed || !sess.Active || (!sess.Deadline.IsZero() && !now.Before(sess.Deadline)) {
		return nil
	}
	cp := *sess.Addr
	return &cp
}

func (s *Server) TargetAllowed(ip net.IP, targetPort, clientPort uint16) bool {
	return s.TargetPolicy.Allows(ip, targetPort, clientPort)
}

func (s *Server) ActiveGameplaySessions() int { return int(s.gameplaySessions.Load()) }

// PreflightFlowOpen rejects abusive/capacity-bound flow creation before the
// relay allocates a UDP socket. It is intentionally repeated by final admission
// for correctness under races. Rate order is session -> source IP -> global so
// one noisy source cannot consume shared budget first.
func (s *Server) PreflightFlowOpen(sessionID uint64, flowID uint32, now time.Time, maintenance bool) FlowAdmission {
	s.admissionMu.Lock()
	defer s.admissionMu.Unlock()
	sess := s.getSession(sessionID)
	if sess == nil {
		return FlowRejectedTemporary
	}
	sess.mu.Lock()
	defer sess.mu.Unlock()
	if sess.closed {
		return FlowRejectedTemporary
	}
	wasActive := len(sess.Flows) > 0
	if !wasActive {
		if maintenance {
			return FlowRejectedMaintenance
		}
		if s.MaxGameplaySessions > 0 && int(s.gameplaySessions.Load()) >= s.MaxGameplaySessions {
			return FlowRejectedCapacity
		}
	}
	_, replacing := sess.Flows[flowID]
	if !replacing && s.MaxFlowsPerSess > 0 && len(sess.Flows) >= s.MaxFlowsPerSess {
		return FlowRejectedTemporary
	}
	burst := s.SessionBurst.Seconds()
	if burst <= 0 {
		burst = 0.25
	}
	if s.FlowOpenPerSessionPPS > 0 {
		rate := float64(s.FlowOpenPerSessionPPS)
		if !sess.FlowOpenRate.allow(now, rate, maxFloat(2, rate*burst), 1) {
			s.flowAdmissionRateDrops.Add(1)
			return FlowRejectedTemporary
		}
	}
	ip := endpointAddr(sess.Addr)
	if !ip.IsValid() {
		return FlowRejectedTemporary
	}
	if s.FlowOpenPerIPPPS > 0 {
		b := s.flowPerIP[ip]
		if b == nil {
			b = &tokenBucket{}
			s.flowPerIP[ip] = b
		}
		rate := float64(s.FlowOpenPerIPPPS)
		if !b.allow(now, rate, maxFloat(2, rate*burst), 1) {
			s.flowAdmissionRateDrops.Add(1)
			return FlowRejectedTemporary
		}
	}
	if s.FlowOpenGlobalPPS > 0 {
		rate := float64(s.FlowOpenGlobalPPS)
		if !s.flowGlobal.allow(now, rate, maxFloat(4, rate*burst), 1) {
			s.flowAdmissionRateDrops.Add(1)
			return FlowRejectedTemporary
		}
	}
	return FlowAccepted
}

func maxFloat(a, b float64) float64 {
	if a > b {
		return a
	}
	return b
}

// AdmitFlowDetailed is a control-path operation. admissionMu serializes only
// node-wide gameplay-capacity transitions; packet forwarding does not take it.
func (s *Server) AdmitFlowDetailed(sessionID uint64, flow *Flow, maintenance bool) (FlowAdmission, bool) {
	if flow == nil {
		return FlowRejectedTemporary, false
	}
	s.admissionMu.Lock()
	sess := s.getSession(sessionID)
	if sess == nil {
		s.admissionMu.Unlock()
		return FlowRejectedTemporary, false
	}
	sess.mu.Lock()
	if sess.closed {
		sess.mu.Unlock()
		s.admissionMu.Unlock()
		return FlowRejectedTemporary, false
	}
	wasActive := len(sess.Flows) > 0
	if !wasActive {
		if maintenance {
			sess.mu.Unlock()
			s.admissionMu.Unlock()
			return FlowRejectedMaintenance, false
		}
		if s.MaxGameplaySessions > 0 && int(s.gameplaySessions.Load()) >= s.MaxGameplaySessions {
			sess.mu.Unlock()
			s.admissionMu.Unlock()
			return FlowRejectedCapacity, false
		}
	}
	var closeAfter *net.UDPConn
	if old := sess.Flows[flow.ID]; old != nil {
		closeAfter = old.Conn
		delete(sess.Flows, flow.ID)
		s.flowsClosed.Add(1)
		s.activeFlows.Add(-1)
	}
	if s.MaxFlowsPerSess > 0 && len(sess.Flows) >= s.MaxFlowsPerSess {
		becameIdle := wasActive && len(sess.Flows) == 0
		if becameIdle {
			s.gameplaySessions.Add(-1)
		}
		sess.mu.Unlock()
		s.admissionMu.Unlock()
		if closeAfter != nil {
			_ = closeAfter.Close()
		}
		return FlowRejectedTemporary, false
	}
	sess.Flows[flow.ID] = flow
	s.flowsOpened.Add(1)
	s.activeFlows.Add(1)
	becameActive := !wasActive
	if becameActive {
		s.gameplaySessions.Add(1)
	}
	sess.mu.Unlock()
	s.admissionMu.Unlock()
	// UDP close is deliberately outside every core lock.
	if closeAfter != nil {
		_ = closeAfter.Close()
	}
	return FlowAccepted, becameActive
}

func (s *Server) AdmitFlow(sessionID uint64, flow *Flow, maintenance bool) FlowAdmission {
	admission, _ := s.AdmitFlowDetailed(sessionID, flow, maintenance)
	return admission
}
func (s *Server) AddFlow(sessionID uint64, flow *Flow) bool {
	return s.AdmitFlow(sessionID, flow, false) == FlowAccepted
}

func (s *Server) GetFlow(sessionID uint64, flowID uint32, now time.Time) *Flow {
	sess := s.getSession(sessionID)
	if sess == nil {
		return nil
	}
	sess.mu.Lock()
	defer sess.mu.Unlock()
	if sess.closed {
		return nil
	}
	flow := sess.Flows[flowID]
	if flow != nil {
		flow.LastSeen = now
		sess.LastSeen = now
	}
	return flow
}

func (s *Server) FlowCurrent(sessionID uint64, flowID uint32, flow *Flow) bool {
	sess := s.getSession(sessionID)
	if sess == nil {
		return false
	}
	sess.mu.Lock()
	defer sess.mu.Unlock()
	return !sess.closed && flow != nil && sess.Flows[flowID] == flow
}

func (s *Server) RemoveFlowDetailed(sessionID uint64, flowID uint32) (bool, bool) {
	s.admissionMu.Lock()
	sess := s.getSession(sessionID)
	if sess == nil {
		s.admissionMu.Unlock()
		return false, false
	}
	sess.mu.Lock()
	if sess.closed {
		sess.mu.Unlock()
		s.admissionMu.Unlock()
		return false, false
	}
	wasActive := len(sess.Flows) > 0
	flow := sess.Flows[flowID]
	if flow == nil {
		sess.mu.Unlock()
		s.admissionMu.Unlock()
		return false, false
	}
	delete(sess.Flows, flowID)
	s.flowsClosed.Add(1)
	s.activeFlows.Add(-1)
	becameIdle := wasActive && len(sess.Flows) == 0
	if becameIdle {
		s.gameplaySessions.Add(-1)
	}
	conn := flow.Conn
	sess.mu.Unlock()
	s.admissionMu.Unlock()
	if conn != nil {
		_ = conn.Close()
	}
	return true, becameIdle
}
func (s *Server) RemoveFlow(sessionID uint64, flowID uint32) {
	_, _ = s.RemoveFlowDetailed(sessionID, flowID)
}

func (s *Server) RemoveSession(addr *net.UDPAddr, id uint64) bool {
	s.admissionMu.Lock()
	s.mu.Lock()
	sess := s.sessions[id]
	if sess == nil {
		s.mu.Unlock()
		s.admissionMu.Unlock()
		return false
	}
	sess.mu.Lock()
	if sess.closed || !sameEndpoint(sess.Addr, addr) {
		sess.mu.Unlock()
		s.mu.Unlock()
		s.admissionMu.Unlock()
		return false
	}
	sess.closed = true
	wasActive := len(sess.Flows) > 0
	conns := make([]*net.UDPConn, 0, len(sess.Flows))
	for _, f := range sess.Flows {
		if f != nil && f.Conn != nil {
			conns = append(conns, f.Conn)
		}
	}
	flowCount := len(sess.Flows)
	clear(sess.Flows)
	key := endpointAddr(sess.Addr)
	delete(s.sessions, id)
	if key.IsValid() && s.controlPerIP[key] > 0 {
		s.controlPerIP[key]--
		if s.controlPerIP[key] == 0 {
			delete(s.controlPerIP, key)
		}
	}
	sess.mu.Unlock()
	s.mu.Unlock()
	if wasActive {
		s.gameplaySessions.Add(-1)
	}
	if flowCount != 0 {
		s.activeFlows.Add(-int64(flowCount))
		s.flowsClosed.Add(uint64(flowCount))
	}
	s.sessionsClosed.Add(1)
	s.admissionMu.Unlock()
	for _, c := range conns {
		_ = c.Close()
	}
	return true
}

// ExpireIdleSessions removes control sessions whose authenticated client traffic
// has been silent for SessionIdleTimeout.  LastSeen is refreshed by every
// validated client packet, including the existing PING heartbeat, so no new
// wire message is required for liveness.  A zero timeout disables this guard.
func (s *Server) ExpireIdleSessions(now time.Time) []uint64 {
	timeout := s.SessionIdleTimeout
	if timeout <= 0 {
		return nil
	}
	s.mu.RLock()
	ids := make([]uint64, 0, len(s.sessions))
	for id := range s.sessions {
		ids = append(ids, id)
	}
	s.mu.RUnlock()

	expired := make([]uint64, 0)
	var closeAfter []*net.UDPConn
	for _, id := range ids {
		s.admissionMu.Lock()
		s.mu.Lock()
		sess := s.sessions[id]
		if sess == nil {
			s.mu.Unlock()
			s.admissionMu.Unlock()
			continue
		}
		sess.mu.Lock()
		idle := !sess.closed && !sess.LastSeen.IsZero() && now.Sub(sess.LastSeen) >= timeout
		if !idle {
			sess.mu.Unlock()
			s.mu.Unlock()
			s.admissionMu.Unlock()
			continue
		}
		sess.closed = true
		wasActive := len(sess.Flows) > 0
		flowCount := len(sess.Flows)
		for _, f := range sess.Flows {
			if f != nil && f.Conn != nil {
				closeAfter = append(closeAfter, f.Conn)
			}
		}
		clear(sess.Flows)
		key := endpointAddr(sess.Addr)
		delete(s.sessions, id)
		if key.IsValid() && s.controlPerIP[key] > 0 {
			s.controlPerIP[key]--
			if s.controlPerIP[key] == 0 {
				delete(s.controlPerIP, key)
			}
		}
		sess.mu.Unlock()
		s.mu.Unlock()
		if wasActive {
			s.gameplaySessions.Add(-1)
		}
		if flowCount != 0 {
			s.activeFlows.Add(-int64(flowCount))
			s.flowsClosed.Add(uint64(flowCount))
		}
		s.sessionsExpired.Add(1)
		expired = append(expired, id)
		s.admissionMu.Unlock()
	}
	for _, c := range closeAfter {
		_ = c.Close()
	}
	return expired
}

func (s *Server) Cleanup(now time.Time) (sessions, flows int) {
	// Snapshot pointers briefly, then do all potentially contended work under
	// per-session locks. Lifecycle transitions use admissionMu but no socket is
	// ever closed while a core lock is held.
	s.mu.RLock()
	snapshot := make([]*Session, 0, len(s.sessions))
	for _, sess := range s.sessions {
		snapshot = append(snapshot, sess)
	}
	s.mu.RUnlock()

	var closeAfter []*net.UDPConn
	for _, sess := range snapshot {
		s.admissionMu.Lock()
		sess.mu.Lock()
		if sess.closed {
			sess.mu.Unlock()
			s.admissionMu.Unlock()
			continue
		}
		hardExpired := !sess.Deadline.IsZero() && !now.Before(sess.Deadline)
		if hardExpired {
			wasActive := len(sess.Flows) > 0
			flowCount := len(sess.Flows)
			for _, f := range sess.Flows {
				if f != nil && f.Conn != nil {
					closeAfter = append(closeAfter, f.Conn)
				}
			}
			clear(sess.Flows)
			sess.closed = true
			id := sess.ID
			sess.mu.Unlock()
			s.mu.Lock()
			if s.sessions[id] == sess {
				key := endpointAddr(sess.Addr)
				delete(s.sessions, id)
				if key.IsValid() && s.controlPerIP[key] > 0 {
					s.controlPerIP[key]--
					if s.controlPerIP[key] == 0 {
						delete(s.controlPerIP, key)
					}
				}
				sessions++
				s.sessionsExpired.Add(1)
			}
			s.mu.Unlock()
			if wasActive {
				s.gameplaySessions.Add(-1)
			}
			if flowCount != 0 {
				s.activeFlows.Add(-int64(flowCount))
				s.flowsClosed.Add(uint64(flowCount))
			}
			s.admissionMu.Unlock()
			continue
		}

		wasActive := len(sess.Flows) > 0
		removed := 0
		for fid, flow := range sess.Flows {
			if s.FlowTimeout > 0 && now.Sub(flow.LastSeen) > s.FlowTimeout {
				if flow.Conn != nil {
					closeAfter = append(closeAfter, flow.Conn)
				}
				delete(sess.Flows, fid)
				removed++
			}
		}
		becameIdle := wasActive && len(sess.Flows) == 0
		sess.mu.Unlock()
		if removed != 0 {
			flows += removed
			s.activeFlows.Add(-int64(removed))
			s.flowsClosed.Add(uint64(removed))
		}
		if becameIdle {
			s.gameplaySessions.Add(-1)
		}
		s.admissionMu.Unlock()
	}

	s.mu.Lock()
	cutoff := now.Unix() - 10
	for k, r := range s.perIP {
		if r.sec < cutoff {
			delete(s.perIP, k)
		}
	}
	s.mu.Unlock()
	s.admissionMu.Lock()
	for k, b := range s.flowPerIP {
		if !b.last.IsZero() && now.Sub(b.last) > time.Minute {
			delete(s.flowPerIP, k)
		}
	}
	s.admissionMu.Unlock()
	for _, c := range closeAfter {
		_ = c.Close()
	}
	return
}

func (s *Server) withSession(sessionID uint64, fn func(*Session)) {
	sess := s.getSession(sessionID)
	if sess == nil {
		return
	}
	sess.mu.Lock()
	if !sess.closed {
		fn(sess)
	}
	sess.mu.Unlock()
}

func (s *Server) CountClientToRelay(sessionID uint64) {
	s.withSession(sessionID, func(sess *Session) { sess.Counters.ClientToRelay++ })
}
func (s *Server) CountRelayToGame(sessionID uint64) {
	s.withSession(sessionID, func(sess *Session) { sess.Counters.RelayToGame++ })
}
func (s *Server) CountGameToRelay(sessionID uint64) {
	s.withSession(sessionID, func(sess *Session) { sess.Counters.GameToRelay++ })
}
func (s *Server) CountRelayToClient(sessionID uint64) {
	s.withSession(sessionID, func(sess *Session) { sess.Counters.RelayToClient++ })
}

func (s *Server) ObserveClientSequence(sessionID, sequence uint64) {
	s.withSession(sessionID, func(sess *Session) { sess.C2RSequence.observe(sequence) })
}

func (s *Server) NextRelaySequence(sessionID uint64) uint64 {
	sess := s.getSession(sessionID)
	if sess == nil {
		return 0
	}
	sess.mu.Lock()
	defer sess.mu.Unlock()
	if sess.closed {
		return 0
	}
	sess.NextS2CSeq++
	if sess.NextS2CSeq == 0 {
		sess.NextS2CSeq++
	}
	return sess.NextS2CSeq
}

func (s *Server) DataStats(sessionID uint64) (DataCounters, bool) {
	sess := s.getSession(sessionID)
	if sess == nil {
		return DataCounters{}, false
	}
	sess.mu.Lock()
	defer sess.mu.Unlock()
	if sess.closed {
		return DataCounters{}, false
	}
	out := sess.Counters
	out.C2RSeqReceived = sess.C2RSequence.finalizedReceived
	out.C2RSeqLost = sess.C2RSequence.finalizedLost
	return out, true
}

func (s *Server) Stats() ServerStats {
	s.mu.RLock()
	control := len(s.sessions)
	s.mu.RUnlock()
	return ServerStats{
		ControlSessions:        control,
		GameplaySessions:       int(s.gameplaySessions.Load()),
		ActiveFlows:            int(s.activeFlows.Load()),
		SessionsCreated:        s.sessionsCreated.Load(),
		SessionsClosed:         s.sessionsClosed.Load(),
		SessionsExpired:        s.sessionsExpired.Load(),
		FlowsOpened:            s.flowsOpened.Load(),
		FlowsClosed:            s.flowsClosed.Load(),
		RateLimitedPackets:     s.rateLimitedPackets.Load(),
		RateLimitedBytes:       s.rateLimitedBytes.Load(),
		SessionAdmissionDrops:  s.sessionAdmissionDrops.Load(),
		FlowAdmissionRateDrops: s.flowAdmissionRateDrops.Load(),
	}
}

func (s *Server) SessionCount() int { return s.Stats().ControlSessions }
