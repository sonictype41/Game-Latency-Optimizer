package main

import (
	"testing"
	"time"

	glocore "glo.local/glo/relay/core/glo_core"
)

func TestUsageTrackerCompletedSession(t *testing.T) {
	u := newUsageTracker()
	start := time.Unix(1000, 0)
	u.start(7, start)
	u.addTunnelRX(7, 100)
	u.addTunnelTX(7, 200)
	u.packetLoss(7, 1.75)
	u.gameplayActive(7, start.Add(time.Second))
	u.addGameRX(7, 300)
	u.addGameTX(7, 400)
	u.gameplayIdle(7, start.Add(11*time.Second))
	if !u.finish(7, start.Add(21*time.Second), "bye") {
		t.Fatal("finish failed")
	}
	r := u.snapshot("", 0)
	if r.Latest != 2 || r.Active != 0 || len(r.Events) != 2 {
		t.Fatalf("bad snapshot: %+v", r)
	}
	if r.Events[0].Type != "session_started" {
		t.Fatalf("missing start event: %+v", r.Events)
	}
	e := r.Events[1]
	if e.Type != "session_ended" {
		t.Fatalf("missing end event: %+v", e)
	}
	if e.DurationMs != 21000 || e.GameplayMs != 10000 {
		t.Fatalf("bad durations: %+v", e)
	}
	if e.TunnelRXBytes != 100 || e.TunnelTXBytes != 200 || e.GameRXBytes != 300 || e.GameTXBytes != 400 {
		t.Fatalf("bad byte counters: %+v", e)
	}
	if e.PacketLossPct == nil || *e.PacketLossPct != 1.75 {
		t.Fatalf("packet loss not exposed: %+v", e)
	}
	if got := u.snapshot(r.Instance, r.Latest); len(got.Events) != 0 {
		t.Fatalf("cursor replayed events: %+v", got.Events)
	}
}

func TestUsageTrackerInstanceMismatchReplaysRing(t *testing.T) {
	u := newUsageTracker()
	now := time.Unix(2000, 0)
	u.start(9, now)
	u.finish(9, now.Add(time.Second), "expired")
	got := u.snapshot("old-instance", 999)
	if len(got.Events) != 2 {
		t.Fatalf("instance rollover did not replay retained events: %+v", got)
	}
}

func TestUsageJournalClosesUnmatchedSessionAfterRestart(t *testing.T) {
	dir := t.TempDir()
	path := dir + "/events.jsonl"
	now := time.Unix(5000, 0)
	u := newUsageTracker(path)
	u.startWithTicket(42, "00112233445566778899aabbccddeeff", now)
	// Simulate process death: construct a new tracker from the durable journal.
	u2 := newUsageTracker(path)
	r := u2.snapshot("", 0)
	if len(r.Events) < 2 {
		t.Fatalf("journal events=%+v", r.Events)
	}
	last := r.Events[len(r.Events)-1]
	if last.Type != "session_ended" || last.TicketID != "00112233445566778899aabbccddeeff" || last.Reason != "relay_restart" {
		t.Fatalf("restart terminal event=%+v", last)
	}
}

func TestUsageTrackerAggregatesGenericQuality(t *testing.T) {
	u := newUsageTracker()
	start := time.Unix(6000, 0)
	u.start(77, start)
	u.quality(77,
		glocore.QualityReport{RelayRTTMicros: 50000, S2CSeqReceived: 48, S2CSeqLost: 2},
		glocore.DataCounters{C2RSeqReceived: 49, C2RSeqLost: 1})
	u.quality(77,
		glocore.QualityReport{RelayRTTMicros: 60000, S2CSeqReceived: 96, S2CSeqLost: 4},
		glocore.DataCounters{C2RSeqReceived: 98, C2RSeqLost: 2})
	if !u.finish(77, start.Add(10*time.Second), "bye") {
		t.Fatal("finish failed")
	}
	e := u.snapshot("", 0).Events[1]
	if e.RelayLatencyMs == nil || *e.RelayLatencyMs != 55.0 {
		t.Fatalf("relay latency aggregate=%+v", e.RelayLatencyMs)
	}
	if e.PacketLossPct == nil {
		t.Fatalf("missing session packet loss: %+v", e)
	}
	// 6 lost / (194 received + 6 lost) = 3%.
	if *e.PacketLossPct < 2.999 || *e.PacketLossPct > 3.001 {
		t.Fatalf("packet loss aggregate=%f", *e.PacketLossPct)
	}
}
