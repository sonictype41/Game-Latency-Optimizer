package glocore

import (
	"net"
	"sync"
	"testing"
	"time"
)

func mustActivate(t *testing.T, s *Server, peer *net.UDPAddr, id uint64, now time.Time) {
	t.Helper()
	if !s.ActivateSession(peer, id, now) {
		t.Fatal("session activation failed")
	}
}

func TestCodecRoundTrip(t *testing.T) {
	in := Packet{Type: StatsResponse, Flags: 3, SessionID: 0x1122334455667788, Nonce: 9, FlowID: 77, Payload: []byte{1, 2, 3, 4}}
	b, err := Encode(in)
	if err != nil {
		t.Fatal(err)
	}
	out, err := Decode(b)
	if err != nil {
		t.Fatal(err)
	}
	if out.Type != in.Type || out.Flags != in.Flags || out.SessionID != in.SessionID || out.Nonce != in.Nonce || out.FlowID != in.FlowID || string(out.Payload) != string(in.Payload) {
		t.Fatalf("roundtrip mismatch: %#v", out)
	}
	b[31] = 1
	if _, err := Decode(b); err == nil {
		t.Fatal("reserved bits were accepted")
	}
	if _, err := Encode(Packet{Type: PacketType(7), SessionID: 1}); err == nil {
		t.Fatal("retired packet type 7 was accepted")
	}
	if _, err := Encode(Packet{Type: PacketType(8), SessionID: 1}); err == nil {
		t.Fatal("retired packet type 8 was accepted")
	}
	valid, err := Encode(Packet{Type: Ping, SessionID: 1})
	if err != nil {
		t.Fatal(err)
	}
	valid[5] = 7
	if _, err := Decode(valid); err == nil {
		t.Fatal("retired wire packet type 7 was decoded")
	}
	valid[5] = 8
	if _, err := Decode(valid); err == nil {
		t.Fatal("retired wire packet type 8 was decoded")
	}
}

func TestSessionEndpointBindingAndRate(t *testing.T) {
	s := NewServer()
	s.MaxPPSPerIP = 10
	s.MaxSessionPPS = 2
	s.MaxSessionKbps = 1024
	now := time.Unix(100, 0)
	a := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 7), Port: 50000}
	id, ok := s.NewSession(a, now)
	if !ok || id == 0 {
		t.Fatal("new session failed")
	}
	if _, ok := s.ValidateSession(a, id, now, 100); !ok {
		t.Fatal("first packet denied")
	}
	if _, ok := s.ValidateSession(a, id, now, 100); !ok {
		t.Fatal("second packet denied")
	}
	if _, ok := s.ValidateSession(a, id, now, 100); ok {
		t.Fatal("session PPS limit not enforced")
	}
	b := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 8), Port: 50000}
	if _, ok := s.ValidateSession(b, id, now.Add(time.Second), 100); ok {
		t.Fatal("session hijack endpoint accepted")
	}
}

func TestFlowEndpointCodec(t *testing.T) {
	p, err := EncodeFlowEndpoint(net.IPv4(128, 116, 48, 33), 53928, 61234)
	if err != nil {
		t.Fatal(err)
	}
	ip, tp, cp, err := DecodeFlowEndpoint(p)
	if err != nil {
		t.Fatal(err)
	}
	if !ip.Equal(net.IPv4(128, 116, 48, 33)) || tp != 53928 || cp != 61234 {
		t.Fatalf("bad flow decode %v %d %d", ip, tp, cp)
	}
}

func TestDataCounters(t *testing.T) {
	s := NewServer()
	now := time.Unix(300, 0)
	a := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 10), Port: 52000}
	id, ok := s.NewSession(a, now)
	if !ok {
		t.Fatal("new session failed")
	}
	s.CountClientToRelay(id)
	s.CountRelayToGame(id)
	s.CountGameToRelay(id)
	s.CountRelayToClient(id)
	for _, seq := range []uint64{1, 3, 2, 4} {
		s.ObserveClientSequence(id, seq)
	}
	stats, ok := s.DataStats(id)
	if !ok {
		t.Fatal("missing stats")
	}
	if stats.ClientToRelay != 1 || stats.RelayToGame != 1 || stats.GameToRelay != 1 || stats.RelayToClient != 1 ||
		stats.C2RSeqReceived != 4 || stats.C2RSeqLost != 0 {
		t.Fatalf("bad stats: %#v", stats)
	}
	wire := EncodeDataCounters(stats)
	decoded, err := DecodeDataCounters(wire)
	if err != nil || decoded != stats {
		t.Fatalf("stats codec mismatch: %#v %v", decoded, err)
	}
}

func TestSequenceLossTrackerReorderAndLoss(t *testing.T) {
	var tracker sequenceLossTracker
	for _, seq := range []uint64{1, 3, 2, 4} {
		tracker.observe(seq)
	}
	if tracker.finalizedReceived != 4 || tracker.finalizedLost != 0 {
		t.Fatalf("reordering counted as loss: %#v", tracker)
	}

	var loss sequenceLossTracker
	loss.observe(1)
	loss.observe(3)
	for seq := uint64(4); seq <= 67; seq++ {
		loss.observe(seq)
	}
	if loss.finalizedLost != 1 || loss.finalizedReceived != 66 {
		t.Fatalf("bad loss accounting: %#v", loss)
	}
}

func TestDataCountersLegacyDecode(t *testing.T) {
	full := EncodeDataCounters(DataCounters{
		ClientToRelay: 1, RelayToGame: 2, GameToRelay: 3, RelayToClient: 4,
		C2RSeqReceived: 5, C2RSeqLost: 6,
	})
	legacy, err := DecodeDataCounters(full[:LegacyDataCountersSize])
	if err != nil {
		t.Fatal(err)
	}
	if legacy.ClientToRelay != 1 || legacy.RelayToClient != 4 || legacy.C2RSeqReceived != 0 || legacy.C2RSeqLost != 0 {
		t.Fatalf("bad legacy decode: %#v", legacy)
	}
}

func TestRelaySequenceMonotonic(t *testing.T) {
	s := NewServer()
	now := time.Unix(350, 0)
	peer := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 12), Port: 52500}
	id, ok := s.NewSession(peer, now)
	if !ok {
		t.Fatal("new session failed")
	}
	if a, b := s.NextRelaySequence(id), s.NextRelaySequence(id); a != 1 || b != 2 {
		t.Fatalf("bad relay sequence: %d %d", a, b)
	}
}

func TestFlowReplacementInvalidatesOldGeneration(t *testing.T) {
	s := NewServer()
	now := time.Unix(400, 0)
	peer := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 11), Port: 53000}
	id, ok := s.NewSession(peer, now)
	if !ok {
		t.Fatal("new session failed")
	}

	c1, err := net.DialUDP("udp4", nil, &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 9})
	if err != nil {
		t.Fatal(err)
	}
	f1 := &Flow{ID: 7, Target: &net.UDPAddr{IP: net.IPv4(128, 116, 1, 1), Port: 50000}, ClientPort: 60000, Conn: c1, LastSeen: now}
	if !s.AddFlow(id, f1) || !s.FlowCurrent(id, 7, f1) {
		t.Fatal("first flow not current")
	}

	c2, err := net.DialUDP("udp4", nil, &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 9})
	if err != nil {
		t.Fatal(err)
	}
	f2 := &Flow{ID: 7, Target: &net.UDPAddr{IP: net.IPv4(128, 116, 2, 2), Port: 50001}, ClientPort: 60001, Conn: c2, LastSeen: now.Add(time.Second)}
	if !s.AddFlow(id, f2) {
		t.Fatal("replacement flow rejected")
	}
	defer s.RemoveFlow(id, 7)

	if s.FlowCurrent(id, 7, f1) {
		t.Fatal("stale flow generation still current")
	}
	if !s.FlowCurrent(id, 7, f2) {
		t.Fatal("replacement flow not current")
	}
	if got := s.GetFlow(id, 7, now.Add(2*time.Second)); got != f2 {
		t.Fatal("GetFlow did not return replacement generation")
	}
}

func TestGameplayAdmissionCapacityAndMaintenance(t *testing.T) {
	s := NewServer()
	s.MaxGameplaySessions = 1
	now := time.Unix(500, 0)
	peer1 := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 21), Port: 54001}
	peer2 := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 22), Port: 54002}
	id1, ok := s.NewSession(peer1, now)
	if !ok {
		t.Fatal("session 1 failed")
	}
	id2, ok := s.NewSession(peer2, now)
	if !ok {
		t.Fatal("session 2 failed")
	}

	mkFlow := func(id uint32, targetLast byte) *Flow {
		c, err := net.DialUDP("udp4", nil, &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 9})
		if err != nil {
			t.Fatal(err)
		}
		return &Flow{ID: id, Target: &net.UDPAddr{IP: net.IPv4(128, 116, 1, targetLast), Port: 50000 + int(id)}, ClientPort: 60000 + uint16(id), Conn: c, LastSeen: now}
	}

	f1 := mkFlow(1, 1)
	if got := s.AdmitFlow(id1, f1, false); got != FlowAccepted {
		t.Fatalf("first gameplay admission = %v", got)
	}
	defer s.RemoveFlow(id1, 1)
	if s.ActiveGameplaySessions() != 1 {
		t.Fatalf("active gameplay = %d", s.ActiveGameplaySessions())
	}

	f2 := mkFlow(2, 2)
	if got := s.AdmitFlow(id2, f2, false); got != FlowRejectedCapacity {
		_ = f2.Conn.Close()
		t.Fatalf("capacity admission = %v", got)
	}
	_ = f2.Conn.Close()

	// Existing admitted gameplay may add another flow even when capacity is full.
	f1b := mkFlow(3, 3)
	if got := s.AdmitFlow(id1, f1b, true); got != FlowAccepted {
		_ = f1b.Conn.Close()
		t.Fatalf("existing gameplay blocked by maintenance = %v", got)
	}
	defer s.RemoveFlow(id1, 3)

	s.RemoveFlow(id1, 1)
	s.RemoveFlow(id1, 3)
	if s.ActiveGameplaySessions() != 0 {
		t.Fatalf("slot not released: %d", s.ActiveGameplaySessions())
	}

	f3 := mkFlow(4, 4)
	if got := s.AdmitFlow(id2, f3, true); got != FlowRejectedMaintenance {
		_ = f3.Conn.Close()
		t.Fatalf("maintenance admission = %v", got)
	}
	_ = f3.Conn.Close()

	f4 := mkFlow(5, 5)
	if got := s.AdmitFlow(id2, f4, false); got != FlowAccepted {
		_ = f4.Conn.Close()
		t.Fatalf("slot was not reusable = %v", got)
	}
	s.RemoveFlow(id2, 5)
}

func TestSessionTokenBucketAllowsBurstThenRefills(t *testing.T) {
	s := NewServer()
	s.MaxPPSPerIP = 10000
	s.MaxSessionPPS = 100
	s.MaxSessionKbps = 0
	s.SessionBurst = 200 * time.Millisecond // 20 packets burst
	now := time.Unix(600, 0)
	peer := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 30), Port: 55000}
	id, ok := s.NewSession(peer, now)
	if !ok {
		t.Fatal("new session failed")
	}
	for i := 0; i < 20; i++ {
		if _, ok := s.ValidateSession(peer, id, now, 64); !ok {
			t.Fatalf("burst packet %d denied", i)
		}
	}
	if _, ok := s.ValidateSession(peer, id, now, 64); ok {
		t.Fatal("packet beyond token bucket burst was accepted")
	}
	if _, ok := s.ValidateSession(peer, id, now.Add(50*time.Millisecond), 64); !ok {
		t.Fatal("token bucket did not refill")
	}
	stats := s.Stats()
	if stats.RateLimitedPackets == 0 {
		t.Fatal("rate-limit accounting did not record denial")
	}
}

func TestServerStatsAndControlConcurrency(t *testing.T) {
	s := NewServer()
	s.MaxControlSessions = 2
	s.MaxPPSPerIP = 10000
	now := time.Unix(700, 0)
	p1 := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 41), Port: 56001}
	p2 := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 42), Port: 56002}
	p3 := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 43), Port: 56003}
	id1, ok := s.NewSession(p1, now)
	if !ok {
		t.Fatal("session 1 failed")
	}
	_, ok = s.NewSession(p2, now)
	if !ok {
		t.Fatal("session 2 failed")
	}
	if _, ok = s.NewSession(p3, now); ok {
		t.Fatal("control concurrency limit not enforced")
	}
	st := s.Stats()
	if st.ControlSessions != 2 || st.SessionsCreated != 2 || st.SessionAdmissionDrops != 1 {
		t.Fatalf("bad control stats: %#v", st)
	}
	if !s.RemoveSession(p1, id1) {
		t.Fatal("session close failed")
	}
	st = s.Stats()
	if st.ControlSessions != 1 || st.SessionsClosed != 1 {
		t.Fatalf("bad close stats: %#v", st)
	}
}

func TestDetailedGameplayLifecycle(t *testing.T) {
	s := NewServer()
	s.MaxGameplaySessions = 4
	now := time.Unix(800, 0)
	peer := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 50), Port: 57000}
	id, ok := s.NewSession(peer, now)
	if !ok {
		t.Fatal("new session failed")
	}
	mustActivate(t, s, peer, id, now)
	conn, err := net.DialUDP("udp4", nil, &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 9})
	if err != nil {
		t.Fatal(err)
	}
	f := &Flow{ID: 1, Target: &net.UDPAddr{IP: net.IPv4(128, 116, 1, 2), Port: 50000}, ClientPort: 60000, Conn: conn, LastSeen: now}
	admission, becameActive := s.AdmitFlowDetailed(id, f, false)
	if admission != FlowAccepted || !becameActive {
		t.Fatalf("admission=%v active=%v", admission, becameActive)
	}
	st := s.Stats()
	if st.GameplaySessions != 1 || st.ActiveFlows != 1 || st.FlowsOpened != 1 {
		t.Fatalf("bad active stats: %#v", st)
	}
	removed, becameIdle := s.RemoveFlowDetailed(id, 1)
	if !removed || !becameIdle {
		t.Fatalf("removed=%v idle=%v", removed, becameIdle)
	}
	st = s.Stats()
	if st.GameplaySessions != 0 || st.ActiveFlows != 0 || st.FlowsClosed != 1 {
		t.Fatalf("bad idle stats: %#v", st)
	}
}

func TestSharedSourceIPDoesNotRateLimitEstablishedSessions(t *testing.T) {
	s := NewServer()
	s.MaxPPSPerIP = 2 // HELLO/new-session admission only.
	s.MaxSessionPPS = 100
	s.MaxSessionKbps = 0
	now := time.Unix(900, 0)
	ip := net.IPv4(198, 51, 100, 44)
	a := &net.UDPAddr{IP: ip, Port: 41001}
	b := &net.UDPAddr{IP: ip, Port: 41002}
	c := &net.UDPAddr{IP: ip, Port: 41003}

	idA, ok := s.NewSession(a, now)
	if !ok {
		t.Fatal("first shared-IP session rejected")
	}
	idB, ok := s.NewSession(b, now)
	if !ok {
		t.Fatal("second shared-IP session rejected")
	}
	if _, ok := s.NewSession(c, now); ok {
		t.Fatal("new-session source-IP admission limit was not enforced")
	}

	// Established traffic must not consume the shared-IP HELLO bucket. Both
	// sessions can exchange many packets in the same second independently.
	for i := 0; i < 25; i++ {
		if _, ok := s.ValidateSession(a, idA, now, 64); !ok {
			t.Fatalf("session A established packet %d rejected by shared source IP", i)
		}
		if _, ok := s.ValidateSession(b, idB, now, 64); !ok {
			t.Fatalf("session B established packet %d rejected by shared source IP", i)
		}
	}
}

func TestSourceIPAdmissionLimiterZeroDisables(t *testing.T) {
	s := NewServer()
	s.MaxPPSPerIP = 0
	s.MaxControlSessions = 0
	now := time.Unix(901, 0)
	ip := net.IPv4(198, 51, 100, 45)
	for i := 0; i < 100; i++ {
		peer := &net.UDPAddr{IP: ip, Port: 42000 + i}
		if _, ok := s.NewSession(peer, now); !ok {
			t.Fatalf("disabled source-IP admission limiter rejected session %d", i)
		}
	}
}

func TestDecodeViewAliasesInputWhileDecodeOwnsPayload(t *testing.T) {
	wire, err := Encode(Packet{Type: StatsResponse, SessionID: 1, FlowID: 2, Payload: []byte{10, 20, 30}})
	if err != nil {
		t.Fatal(err)
	}
	owned, err := Decode(wire)
	if err != nil {
		t.Fatal(err)
	}
	view, err := DecodeView(wire)
	if err != nil {
		t.Fatal(err)
	}
	wire[HeaderSize] = 99
	if owned.Payload[0] != 10 {
		t.Fatalf("Decode payload unexpectedly aliases input: %v", owned.Payload)
	}
	if view.Payload[0] != 99 {
		t.Fatalf("DecodeView payload did not alias input: %v", view.Payload)
	}
}

func TestValidateDataC2SCombinesHotPathAccounting(t *testing.T) {
	s := NewServer()
	s.MaxSessionPPS = 100
	s.MaxSessionKbps = 100000
	now := time.Unix(900, 0)
	peer := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 90), Port: 59000}
	id, ok := s.NewSession(peer, now)
	if !ok {
		t.Fatal("new session failed")
	}
	mustActivate(t, s, peer, id, now)
	flow, ok := s.ValidateDataC2S(peer, id, now, 100, 1, 7)
	if !ok || flow != nil {
		t.Fatalf("hot-path validate failed flow=%v ok=%v", flow, ok)
	}
	st, ok := s.DataStats(id)
	if !ok || st.ClientToRelay != 1 || st.C2RSeqReceived != 1 {
		t.Fatalf("hot-path accounting mismatch: %+v ok=%v", st, ok)
	}
}

func BenchmarkDecodeOwned(b *testing.B) {
	wire, _ := Encode(Packet{Type: StatsResponse, SessionID: 1, FlowID: 2, Payload: make([]byte, 1200)})
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		if _, err := Decode(wire); err != nil {
			b.Fatal(err)
		}
	}
}

func BenchmarkDecodeView(b *testing.B) {
	wire, _ := Encode(Packet{Type: StatsResponse, SessionID: 1, FlowID: 2, Payload: make([]byte, 1200)})
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		if _, err := DecodeView(wire); err != nil {
			b.Fatal(err)
		}
	}
}

func TestPrepareS2CRefreshesReverseOnlyFlowActivity(t *testing.T) {
	s := NewServer()
	s.FlowTimeout = 15 * time.Second
	now := time.Unix(1000, 0)
	peer := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 99), Port: 60000}
	id, ok := s.NewSession(peer, now)
	if !ok {
		t.Fatal("new session failed")
	}
	mustActivate(t, s, peer, id, now)
	conn, err := net.DialUDP("udp4", nil, &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 9})
	if err != nil {
		t.Fatal(err)
	}
	flow := &Flow{ID: 1, Target: &net.UDPAddr{IP: net.IPv4(128, 116, 1, 1), Port: 50000}, ClientPort: 60001, Conn: conn, LastSeen: now}
	if !s.AddFlow(id, flow) {
		t.Fatal("flow admission failed")
	}
	defer s.RemoveFlow(id, 1)

	reverse := now.Add(14 * time.Second)
	if _, _, ok := s.PrepareS2C(id, 1, flow, reverse); !ok {
		t.Fatal("reverse path preparation failed")
	}
	if sessions, flows := s.Cleanup(now.Add(20 * time.Second)); sessions != 0 || flows != 0 {
		t.Fatalf("active reverse-only flow expired sessions=%d flows=%d", sessions, flows)
	}
}

func TestSessionLocalHotPathConcurrency(t *testing.T) {
	s := NewServer()
	s.MaxPPSPerIP = 0
	s.MaxControlSessions = 0
	s.MaxSessionPPS = 0
	s.MaxSessionKbps = 0
	now := time.Unix(1100, 0)
	const sessions = 32
	const packets = 500
	type item struct {
		peer *net.UDPAddr
		id   uint64
	}
	items := make([]item, 0, sessions)
	for i := 0; i < sessions; i++ {
		peer := &net.UDPAddr{IP: net.IPv4(198, 51, 100, byte(i+1)), Port: 61000 + i}
		id, ok := s.NewSession(peer, now)
		if !ok {
			t.Fatal("session admission failed")
		}
		mustActivate(t, s, peer, id, now)
		items = append(items, item{peer: peer, id: id})
	}
	var wg sync.WaitGroup
	for _, it := range items {
		it := it
		wg.Add(1)
		go func() {
			defer wg.Done()
			for n := 1; n <= packets; n++ {
				if _, ok := s.ValidateDataC2S(it.peer, it.id, now, 100, uint64(n), 1); !ok {
					t.Errorf("hot path rejected session=%d packet=%d", it.id, n)
					return
				}
			}
		}()
	}
	wg.Wait()
	for _, it := range items {
		st, ok := s.DataStats(it.id)
		if !ok || st.ClientToRelay != packets {
			t.Fatalf("session=%d stats=%+v ok=%v", it.id, st, ok)
		}
	}
}

func TestGLODCodecC2SRoundTripAndAlias(t *testing.T) {
	endpoint, err := EncodeFlowEndpoint(net.IPv4(128, 116, 48, 33), 53928, 61234)
	if err != nil {
		t.Fatal(err)
	}
	payload := []byte{10, 20, 30, 40}
	buf := make([]byte, 0, DataMaxDatagram)
	wire, err := EncodeDataInto(buf, DataFrame{Direction: DataDirectionC2S, SessionID: 9, Sequence: 7, FlowID: 3, Endpoint: endpoint, Payload: payload})
	if err != nil {
		t.Fatal(err)
	}
	frame, err := DecodeDataView(wire)
	if err != nil {
		t.Fatal(err)
	}
	if frame.Direction != DataDirectionC2S || frame.SessionID != 9 || frame.Sequence != 7 || frame.FlowID != 3 || string(frame.Endpoint) != string(endpoint) || string(frame.Payload) != string(payload) {
		t.Fatalf("bad GLOD frame: %+v", frame)
	}
	wire[DataHeaderSize+DataRouteMetaSize] = 99
	if frame.Payload[0] != 99 {
		t.Fatal("GLOD payload view unexpectedly copied")
	}
}

func TestGLODRejectsControlAndMalformedMetadata(t *testing.T) {
	if _, err := DecodeDataView([]byte("GLO2")); err == nil {
		t.Fatal("control wire accepted as GLOD")
	}
	buf := make([]byte, DataMaxDatagram)
	var endpoint [DataRouteMetaSize]byte
	wire, err := EncodeDataInto(buf[:0], DataFrame{Direction: DataDirectionC2S, SessionID: 1, Sequence: 1, FlowID: 1, Endpoint: endpoint[:], Payload: []byte{1}})
	if err != nil {
		t.Fatal(err)
	}
	wire[30] = 0
	if _, err := DecodeDataView(wire); err == nil {
		t.Fatal("bad C2S metadata length accepted")
	}
}

func TestDataRequiresActivationAndHonorsDeadline(t *testing.T) {
	s := NewServer()
	s.MaxSessionPPS = 0
	s.MaxSessionKbps = 0
	now := time.Unix(5000, 0)
	peer := &net.UDPAddr{IP: net.IPv4(203, 0, 113, 111), Port: 62000}
	id, ok := s.NewSessionWithGrant(peer, now, "ticket", time.Second)
	if !ok {
		t.Fatal("session admission failed")
	}
	if _, ok := s.ValidateDataC2S(peer, id, now, 100, 1, 1); ok {
		t.Fatal("inactive session accepted DATA")
	}
	mustActivate(t, s, peer, id, now)
	if _, ok := s.ValidateDataC2S(peer, id, now.Add(500*time.Millisecond), 100, 1, 1); !ok {
		t.Fatal("active session rejected DATA")
	}
	if _, ok := s.ValidateDataC2S(peer, id, now.Add(time.Second), 100, 2, 1); ok {
		t.Fatal("expired session accepted DATA")
	}
	if ep := s.DataSessionEndpoint(id, now.Add(time.Second)); ep != nil {
		t.Fatal("expired session remained data-alive")
	}
}

func BenchmarkDecodeGLODView400(b *testing.B) {
	var ep [DataRouteMetaSize]byte
	wire, _ := EncodeDataInto(make([]byte, 0, DataMaxDatagram), DataFrame{Direction: DataDirectionC2S, SessionID: 1, Sequence: 1, FlowID: 2, Endpoint: ep[:], Payload: make([]byte, 400)})
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		if _, err := DecodeDataView(wire); err != nil {
			b.Fatal(err)
		}
	}
}

func FuzzDecodeViewNoPanic(f *testing.F) {
	f.Add([]byte("GLO2"))
	f.Add(make([]byte, MaxDatagram+1))
	f.Fuzz(func(t *testing.T, in []byte) {
		if len(in) > MaxDatagram+64 {
			in = in[:MaxDatagram+64]
		}
		_, _ = DecodeView(in)
	})
}

func TestV061ControlSessionPerIPAndGlobalAllocationLimits(t *testing.T) {
	s := NewServer()
	s.MaxControlSessions = 0
	s.MaxControlSessionsPerIP = 2
	s.MaxPPSPerIP = 0
	s.MaxAllocPerIPPPS = 0
	s.MaxAllocGlobalPPS = 2
	now := time.Unix(1000, 0)
	ip := net.IPv4(198, 51, 100, 80)
	a := &net.UDPAddr{IP: ip, Port: 50001}
	b := &net.UDPAddr{IP: ip, Port: 50002}
	c := &net.UDPAddr{IP: ip, Port: 50003}
	idA, ok := s.NewSession(a, now)
	if !ok {
		t.Fatal("first allocation rejected")
	}
	if _, ok := s.NewSession(b, now); !ok {
		t.Fatal("second allocation rejected")
	}
	if _, ok := s.NewSession(c, now); ok {
		t.Fatal("per-IP concurrent control-session cap not enforced")
	}
	if !s.RemoveSession(a, idA) {
		t.Fatal("remove failed")
	}
	// Global allocation budget for this second is still exhausted even though
	// capacity was freed. A new second replenishes it.
	if _, ok := s.NewSession(c, now); ok {
		t.Fatal("global allocation PPS limit not enforced")
	}
	if _, ok := s.NewSession(c, now.Add(time.Second)); !ok {
		t.Fatal("allocation did not recover next second")
	}
}

func TestPerTicketSessionLifetime(t *testing.T) {
	s := NewServer()
	s.MaxPPSPerIP = 0
	s.MaxAllocGlobalPPS = 0
	s.MaxControlSessionsPerIP = 0
	now := time.Unix(1100, 0)
	peer := &net.UDPAddr{IP: net.IPv4(198, 51, 100, 81), Port: 51001}
	id, ok := s.NewSessionWithGrant(peer, now, "ticket", 2*time.Second)
	if !ok {
		t.Fatal("session allocation failed")
	}
	if _, ok := s.ValidateSession(peer, id, now.Add(1500*time.Millisecond), 64); !ok {
		t.Fatal("active session rejected before hard lifetime")
	}
	removed, _ := s.Cleanup(now.Add(2 * time.Second))
	if removed != 1 || s.SessionCount() != 0 {
		t.Fatalf("hard lifetime cleanup removed=%d count=%d", removed, s.SessionCount())
	}
}

func TestV061FlowOpenPreflightRateLimit(t *testing.T) {
	s := NewServer()
	s.MaxPPSPerIP = 0
	s.MaxAllocGlobalPPS = 0
	s.MaxControlSessionsPerIP = 0
	s.MaxGameplaySessions = 0
	s.FlowOpenGlobalPPS = 0
	s.FlowOpenPerIPPPS = 0
	s.FlowOpenPerSessionPPS = 2
	s.SessionBurst = time.Second
	now := time.Unix(1200, 0)
	peer := &net.UDPAddr{IP: net.IPv4(198, 51, 100, 82), Port: 52001}
	id, ok := s.NewSession(peer, now)
	if !ok {
		t.Fatal("session allocation failed")
	}
	if got := s.PreflightFlowOpen(id, 1, now, false); got != FlowAccepted {
		t.Fatalf("first preflight=%v", got)
	}
	if got := s.PreflightFlowOpen(id, 2, now, false); got != FlowAccepted {
		t.Fatalf("second preflight=%v", got)
	}
	if got := s.PreflightFlowOpen(id, 3, now, false); got == FlowAccepted {
		t.Fatal("per-session flow-open limiter not enforced")
	}
	if got := s.PreflightFlowOpen(id, 3, now.Add(time.Second), false); got != FlowAccepted {
		t.Fatalf("flow-open limiter did not recover: %v", got)
	}
	if s.Stats().FlowAdmissionRateDrops == 0 {
		t.Fatal("flow admission rate drop not counted")
	}
}

func TestSessionIdleTimeoutExpiresSilentClient(t *testing.T) {
	s := NewServer()
	s.MaxPPSPerIP = 0
	s.MaxAllocGlobalPPS = 0
	s.MaxControlSessionsPerIP = 0
	s.SessionIdleTimeout = 25 * time.Second
	now := time.Unix(1400, 0)
	peer := &net.UDPAddr{IP: net.IPv4(198, 51, 100, 90), Port: 53001}
	id, ok := s.NewSession(peer, now)
	if !ok {
		t.Fatal("session allocation failed")
	}
	if got := s.ExpireIdleSessions(now.Add(24 * time.Second)); len(got) != 0 {
		t.Fatalf("session expired too early: %v", got)
	}
	if _, ok := s.ValidateSession(peer, id, now.Add(24*time.Second), 64); !ok {
		t.Fatal("heartbeat validation failed")
	}
	if got := s.ExpireIdleSessions(now.Add(48 * time.Second)); len(got) != 0 {
		t.Fatalf("refreshed session expired too early: %v", got)
	}
	got := s.ExpireIdleSessions(now.Add(50 * time.Second))
	if len(got) != 1 || got[0] != id || s.SessionCount() != 0 {
		t.Fatalf("silent session was not expired got=%v count=%d", got, s.SessionCount())
	}
}

func TestSessionIdleTimeoutCanBeDisabled(t *testing.T) {
	s := NewServer()
	s.MaxPPSPerIP = 0
	s.MaxAllocGlobalPPS = 0
	s.MaxControlSessionsPerIP = 0
	s.SessionIdleTimeout = 0
	now := time.Unix(1500, 0)
	peer := &net.UDPAddr{IP: net.IPv4(198, 51, 100, 91), Port: 53002}
	if _, ok := s.NewSession(peer, now); !ok {
		t.Fatal("session allocation failed")
	}
	if got := s.ExpireIdleSessions(now.Add(24 * time.Hour)); len(got) != 0 || s.SessionCount() != 1 {
		t.Fatalf("disabled timeout removed session got=%v count=%d", got, s.SessionCount())
	}
}

func TestQualityReportCodec(t *testing.T) {
	in := QualityReport{RelayRTTMicros: 52340, S2CSeqReceived: 1234, S2CSeqLost: 7}
	wire := EncodeQualityReport(in)
	if len(wire) != QualityReportSize || wire[0] != QualityReportVersion {
		t.Fatalf("bad quality wire: %v", wire)
	}
	out, err := DecodeQualityReport(wire)
	if err != nil || out != in {
		t.Fatalf("quality codec mismatch: %#v %v", out, err)
	}
	wire[1] = 1
	if _, err := DecodeQualityReport(wire); err == nil {
		t.Fatal("reserved quality byte accepted")
	}
}
