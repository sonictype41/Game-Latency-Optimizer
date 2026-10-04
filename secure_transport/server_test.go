package secure

import (
	"bytes"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"net"
	"testing"
	"time"
)

func testAuthorize(ticket []byte, now time.Time) (Admission, bool) {
	if len(ticket) == 0 {
		return Admission{}, false
	}
	h := sha256.Sum256(ticket)
	var id [16]byte
	copy(id[:], h[:16])
	return Admission{TicketID: id, SessionTTL: 2 * time.Hour}, true
}
func testRedeem(_ []byte, _ []byte, _ time.Time) bool { return true }
func makeHello(t *testing.T) []byte {
	t.Helper()
	cp, _ := ecdh.X25519().GenerateKey(rand.Reader)
	h := make([]byte, HelloSize)
	copy(h, "GLH6")
	h[4] = 1
	copy(h[8:40], cp.PublicKey().Bytes())
	_, _ = rand.Read(h[40:72])
	return h
}
func challenge(t *testing.T, s *Server, peer *net.UDPAddr, h []byte, now time.Time) []byte {
	t.Helper()
	r := s.Handshake(peer, h, now, testAuthorize, func(Admission) (uint64, bool) { t.Fatal("allocated before ticket"); return 0, false }, testRedeem, nil, nil, nil)
	if len(r) != HelloSize || r[4] != 2 {
		t.Fatalf("bad retry %d", len(r))
	}
	return r
}
func authFromRetry(retry []byte) []byte {
	a := make([]byte, AuthHelloPrefix+1)
	copy(a, retry)
	a[4] = 4
	binary.BigEndian.PutUint16(a[104:106], 1)
	a[106] = 1
	return a
}

func TestCookieTicketGateAndEndpointBinding(t *testing.T) {
	k, _ := ecdh.X25519().GenerateKey(rand.Reader)
	s, _ := NewServer(k)
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 40000}
	now := time.Now()
	h := makeHello(t)
	retry := challenge(t, s, peer, h, now)
	auth := authFromRetry(retry)
	allocated := 0
	allocate := func(Admission) (uint64, bool) { allocated++; return 77, true }
	welcome := func(uint64, Admission) []byte { return bytes.Repeat([]byte{0}, 32) }
	other := &net.UDPAddr{IP: peer.IP, Port: 40001}
	if r := s.Handshake(other, auth, now, testAuthorize, allocate, testRedeem, welcome, nil, nil); len(r) != HelloSize || r[4] != 2 || allocated != 0 {
		t.Fatal("cookie moved across endpoint")
	}
	bad := append([]byte{}, auth...)
	bad[8] ^= 1
	if r := s.Handshake(peer, bad, now, testAuthorize, allocate, testRedeem, welcome, nil, nil); len(r) != HelloSize || allocated != 0 {
		t.Fatal("cookie not bound to client key")
	}
	reply := s.Handshake(peer, auth, now, testAuthorize, allocate, testRedeem, welcome, nil, nil)
	if len(reply) != 184 || allocated != 1 {
		t.Fatalf("ticket handshake failed len=%d allocations=%d", len(reply), allocated)
	}
	again := s.Handshake(peer, auth, now, testAuthorize, allocate, testRedeem, welcome, nil, nil)
	if !bytes.Equal(reply, again) || allocated != 1 {
		t.Fatal("retransmission allocated new session")
	}
	ids := s.Cleanup(now.Add(6*time.Second), func(uint64) bool { return true })
	if len(ids) != 1 || ids[0] != 77 {
		t.Fatalf("pending cleanup=%v", ids)
	}
}

func TestMalformedAndPlaintextRejected(t *testing.T) {
	k, _ := ecdh.X25519().GenerateKey(rand.Reader)
	s, _ := NewServer(k)
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 40000}
	for n := 0; n < 1500; n++ {
		b := make([]byte, n)
		rand.Read(b)
		if s.Handshake(peer, b, time.Now(), testAuthorize, func(Admission) (uint64, bool) { t.Fatal("garbage allocated"); return 0, false }, testRedeem, nil, nil, nil) != nil {
			t.Fatal("garbage handshake")
		}
		if _, _, ok := s.Open(peer, b, time.Now()); ok {
			t.Fatal("garbage secure record")
		}
	}
}

func TestPerAdmissionDeadline(t *testing.T) {
	k, _ := ecdh.X25519().GenerateKey(rand.Reader)
	s, _ := NewServer(k)
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 40100}
	now := time.Now()
	retry := challenge(t, s, peer, makeHello(t), now)
	auth := authFromRetry(retry)
	authz := func(ticket []byte, _ time.Time) (Admission, bool) {
		a, _ := testAuthorize(ticket, now)
		a.SessionTTL = 2 * time.Second
		return a, true
	}
	reply := s.Handshake(peer, auth, now, authz, func(Admission) (uint64, bool) { return 99, true }, testRedeem, func(uint64, Admission) []byte { return bytes.Repeat([]byte{0}, 32) }, nil, nil)
	if len(reply) != 184 {
		t.Fatal("handshake failed")
	}
	s.Activate(99)
	if got := s.Cleanup(now.Add(1500*time.Millisecond), func(uint64) bool { return true }); len(got) != 0 {
		t.Fatal("expired early")
	}
	if got := s.Cleanup(now.Add(2*time.Second), func(uint64) bool { return true }); len(got) != 1 || got[0] != 99 {
		t.Fatalf("deadline not enforced %v", got)
	}
}

func TestHandshakeDoesNotHoldServerLockAcrossAllocation(t *testing.T) {
	k, _ := ecdh.X25519().GenerateKey(rand.Reader)
	s, _ := NewServer(k)
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 40200}
	ap, _ := addrPort(peer)
	deadline := time.Now().Add(time.Hour)
	serverCh, _ := NewChannel(88, bytes.Repeat([]byte{1}, 32), bytes.Repeat([]byte{2}, 32), deadline)
	clientCh, _ := NewChannel(88, bytes.Repeat([]byte{2}, 32), bytes.Repeat([]byte{1}, 32), deadline)
	s.entries[88] = &Entry{channel: serverCh, peer: ap, created: time.Now(), deadline: deadline, active: true}
	wire, _ := clientCh.Seal([]byte("game"))
	now := time.Now()
	retry := challenge(t, s, peer, makeHello(t), now)
	auth := authFromRetry(retry)
	entered := make(chan struct{})
	release := make(chan struct{})
	done := make(chan struct{})
	go func() {
		defer close(done)
		s.Handshake(peer, auth, now, testAuthorize, func(Admission) (uint64, bool) { close(entered); <-release; return 100, true }, testRedeem, func(uint64, Admission) []byte { return bytes.Repeat([]byte{0}, 32) }, nil, nil)
	}()
	<-entered
	lookup := make(chan bool, 1)
	go func() {
		_, p, ok := s.Open(peer, append([]byte{}, wire...), time.Now())
		lookup <- ok && string(p) == "game"
	}()
	select {
	case ok := <-lookup:
		if !ok {
			t.Fatal("lookup failed")
		}
	case <-time.After(250 * time.Millisecond):
		t.Fatal("dataplane blocked by allocation")
	}
	close(release)
	<-done
}
