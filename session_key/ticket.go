// Package sessionkey implements GLO current GLO release short-lived single-use bearer grants.
// A config issuer signs a grant for one relay; the selected relay verifies it
// locally and binds it to the first successfully allocated transport session.
package sessionkey

import (
	"bufio"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"sync"
	"time"
)

const (
	Magic        = "GSK2"
	Version      = byte(2)
	UnsignedSize = 88
	TicketSize   = UnsignedSize + ed25519.SignatureSize
	MaxClockSkew = 30 * time.Second
)

var (
	ErrInvalid  = errors.New("invalid session ticket")
	ErrExpired  = errors.New("session ticket expired")
	ErrRelay    = errors.New("session ticket relay mismatch")
	ErrRedeemed = errors.New("session ticket already redeemed")
)

type Ticket struct {
	ID           [16]byte
	IssuerKeyID  [8]byte
	RelayKeyHash [32]byte
	IssuedAt     uint64
	RedeemBefore uint64
	SessionTTL   uint32
	Flags        byte
}

func KeyID(pub ed25519.PublicKey) [8]byte {
	h := sha256.Sum256(pub)
	var out [8]byte
	copy(out[:], h[:8])
	return out
}
func RelayKeyHash(relayRaw []byte) [32]byte { return sha256.Sum256(relayRaw) }

func MarshalUnsigned(t Ticket) []byte {
	b := make([]byte, UnsignedSize)
	copy(b[:4], Magic)
	b[4] = Version
	b[5] = t.Flags
	copy(b[8:24], t.ID[:])
	copy(b[24:32], t.IssuerKeyID[:])
	copy(b[32:64], t.RelayKeyHash[:])
	binary.BigEndian.PutUint64(b[64:72], t.IssuedAt)
	binary.BigEndian.PutUint64(b[72:80], t.RedeemBefore)
	binary.BigEndian.PutUint32(b[80:84], t.SessionTTL)
	return b
}

func Sign(t Ticket, private ed25519.PrivateKey) ([]byte, error) {
	if len(private) != ed25519.PrivateKeySize || t.SessionTTL == 0 || t.RedeemBefore < t.IssuedAt {
		return nil, ErrInvalid
	}
	unsigned := MarshalUnsigned(t)
	return append(unsigned, ed25519.Sign(private, unsigned)...), nil
}

func Parse(b []byte) (Ticket, []byte, error) {
	var t Ticket
	if len(b) != TicketSize || string(b[:4]) != Magic || b[4] != Version || b[6] != 0 || b[7] != 0 || binary.BigEndian.Uint32(b[84:88]) != 0 {
		return t, nil, ErrInvalid
	}
	t.Flags = b[5]
	copy(t.ID[:], b[8:24])
	copy(t.IssuerKeyID[:], b[24:32])
	copy(t.RelayKeyHash[:], b[32:64])
	t.IssuedAt = binary.BigEndian.Uint64(b[64:72])
	t.RedeemBefore = binary.BigEndian.Uint64(b[72:80])
	t.SessionTTL = binary.BigEndian.Uint32(b[80:84])
	if t.SessionTTL == 0 || t.RedeemBefore < t.IssuedAt {
		return Ticket{}, nil, ErrInvalid
	}
	return t, b[UnsignedSize:], nil
}

type claim struct {
	identity  [32]byte
	expires   int64
	priorBoot bool
}
type Verifier struct {
	mu        sync.Mutex
	keys      map[[8]byte]ed25519.PublicKey
	relayHash [32]byte
	claims    map[[16]byte]claim
	journal   string
}

func NewVerifier(keys []ed25519.PublicKey, relayRaw []byte, journal string, now time.Time) (*Verifier, error) {
	if len(relayRaw) != 32 || len(keys) == 0 {
		return nil, ErrInvalid
	}
	v := &Verifier{keys: make(map[[8]byte]ed25519.PublicKey), relayHash: RelayKeyHash(relayRaw), claims: make(map[[16]byte]claim), journal: journal}
	for _, k := range keys {
		if len(k) != ed25519.PublicKeySize {
			return nil, ErrInvalid
		}
		cp := append(ed25519.PublicKey(nil), k...)
		v.keys[KeyID(cp)] = cp
	}
	if err := v.loadJournal(now); err != nil {
		return nil, err
	}
	return v, nil
}

func LoadPublicKeys(path string) ([]ed25519.PublicKey, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	var out []ed25519.PublicKey
	s := bufio.NewScanner(f)
	for s.Scan() {
		raw := s.Text()
		if raw == "" || raw[0] == '#' {
			continue
		}
		b, e := hex.DecodeString(raw)
		if e != nil || len(b) != ed25519.PublicKeySize {
			return nil, fmt.Errorf("invalid issuer public key")
		}
		out = append(out, ed25519.PublicKey(b))
	}
	if err := s.Err(); err != nil {
		return nil, err
	}
	if len(out) == 0 {
		return nil, errors.New("issuer key file is empty")
	}
	return out, nil
}

func (v *Verifier) Verify(raw []byte, now time.Time) (Ticket, error) {
	t, sig, err := Parse(raw)
	if err != nil {
		return Ticket{}, err
	}
	pub := v.keys[t.IssuerKeyID]
	if pub == nil || !ed25519.Verify(pub, raw[:UnsignedSize], sig) {
		return Ticket{}, ErrInvalid
	}
	nowUnix := now.Unix()
	if int64(t.IssuedAt) > nowUnix+int64(MaxClockSkew/time.Second) || nowUnix > int64(t.RedeemBefore) {
		return Ticket{}, ErrExpired
	}
	if t.RelayKeyHash != v.relayHash {
		return Ticket{}, ErrRelay
	}
	return t, nil
}

// Redeem commits a previously verified bearer grant only after the relay has
// successfully allocated the local session resources. identity is supplied by
// the transport and includes the actual handshake identity plus observed peer
// endpoint. The first successful session claims the grant; retransmissions from
// that same session remain idempotent while copied grants are rejected.
func (v *Verifier) Redeem(t Ticket, identity []byte, now time.Time) error {
	if t.ID == ([16]byte{}) || len(identity) == 0 {
		return ErrInvalid
	}
	nowUnix := now.Unix()
	if nowUnix > int64(t.RedeemBefore) {
		return ErrExpired
	}
	ih := sha256.Sum256(identity)
	v.mu.Lock()
	defer v.mu.Unlock()
	for id, c := range v.claims {
		if c.expires < nowUnix {
			delete(v.claims, id)
		}
	}
	if c, ok := v.claims[t.ID]; ok {
		if c.priorBoot || c.identity != ih {
			return ErrRedeemed
		}
		return nil
	}
	v.claims[t.ID] = claim{identity: ih, expires: int64(t.RedeemBefore)}
	if err := v.appendJournal(t.ID, int64(t.RedeemBefore)); err != nil {
		delete(v.claims, t.ID)
		return err
	}
	return nil
}

// VerifyAndRedeem is retained for small tools/tests. Relay admission
// uses Verify -> allocate -> Redeem so an allocation failure cannot consume a
// single-use grant.
func (v *Verifier) VerifyAndRedeem(raw, identity []byte, now time.Time) (Ticket, error) {
	t, err := v.Verify(raw, now)
	if err != nil {
		return Ticket{}, err
	}
	if err = v.Redeem(t, identity, now); err != nil {
		return Ticket{}, err
	}
	return t, nil
}

func (v *Verifier) loadJournal(now time.Time) error {
	if v.journal == "" {
		return nil
	}
	f, err := os.Open(v.journal)
	if errors.Is(err, os.ErrNotExist) {
		return nil
	}
	if err != nil {
		return err
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	nowUnix := now.Unix()
	for s.Scan() {
		var idHex string
		var exp int64
		if _, e := fmt.Sscanf(s.Text(), "%32s %d", &idHex, &exp); e != nil || exp < nowUnix {
			continue
		}
		b, e := hex.DecodeString(idHex)
		if e != nil || len(b) != 16 {
			continue
		}
		var id [16]byte
		copy(id[:], b)
		v.claims[id] = claim{expires: exp, priorBoot: true}
	}
	return s.Err()
}
func (v *Verifier) appendJournal(id [16]byte, expires int64) error {
	if v.journal == "" {
		return nil
	}
	f, err := os.OpenFile(v.journal, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0600)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = fmt.Fprintf(f, "%x %d\n", id, expires)
	return err
}
func IDHex(id [16]byte) string { return hex.EncodeToString(id[:]) }
