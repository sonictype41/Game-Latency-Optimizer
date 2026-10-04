package main

import (
	"bufio"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"sync"
	"time"

	glocore "glo.local/glo/relay/core/glo_core"
)

type sessionUsage struct {
	TicketID           string
	CreatedAt          time.Time
	GameplayStarted    time.Time
	LastGameplayAt     time.Time
	GameplayTotal      time.Duration
	TunnelRXBytes      uint64
	TunnelTXBytes      uint64
	GameRXBytes        uint64
	GameTXBytes        uint64
	PacketLossPct      *float64 // legacy v0.16.x/v0.17.x client report fallback
	RelayRTTMsSum      float64
	RelayRTTSamples    uint64
	C2RSeqReceived     uint64
	C2RSeqLost         uint64
	S2CSeqReceived     uint64
	S2CSeqLost         uint64
	QualityCountersSet bool
}

type usageEvent struct {
	Type            string   `json:"type"`
	EventID         string   `json:"event_id"`
	TicketID        string   `json:"ticket_id,omitempty"`
	Seq             uint64   `json:"seq"`
	SessionID       uint64   `json:"session_id"`
	StartedAtUnixMs int64    `json:"started_at_unix_ms"`
	EndedAtUnixMs   int64    `json:"ended_at_unix_ms"`
	DurationMs      int64    `json:"duration_ms"`
	GameplayMs      int64    `json:"gameplay_ms"`
	TunnelRXBytes   uint64   `json:"tunnel_rx_bytes"`
	TunnelTXBytes   uint64   `json:"tunnel_tx_bytes"`
	GameRXBytes     uint64   `json:"game_rx_bytes"`
	GameTXBytes     uint64   `json:"game_tx_bytes"`
	Reason          string   `json:"reason"`
	RelayLatencyMs  *float64 `json:"relay_latency_ms,omitempty"`
	PacketLossPct   *float64 `json:"packet_loss_pct,omitempty"`
}

type usageTracker struct {
	mu       sync.Mutex
	instance string
	seq      uint64
	sessions map[uint64]*sessionUsage
	events   []usageEvent
	journal  string
}

func randomInstance() string {
	raw := make([]byte, 12)
	_, _ = rand.Read(raw)
	return hex.EncodeToString(raw)
}

func newUsageTracker(journal ...string) *usageTracker {
	path := ""
	if len(journal) != 0 {
		path = journal[0]
	}
	u := &usageTracker{instance: randomInstance(), sessions: make(map[uint64]*sessionUsage), journal: path}
	u.loadJournal(time.Now())
	return u
}

func eventInstance(eventID string) string {
	for i := 0; i < len(eventID); i++ {
		if eventID[i] == ':' {
			if i == 24 {
				return eventID[:i]
			}
			return ""
		}
	}
	return ""
}

func (u *usageTracker) appendEventLocked(e usageEvent) {
	u.events = append(u.events, e)
	if len(u.events) > 4096 {
		u.events = append([]usageEvent(nil), u.events[len(u.events)-4096:]...)
	}
	if u.journal == "" {
		return
	}
	if err := os.MkdirAll(filepath.Dir(u.journal), 0700); err != nil {
		return
	}
	f, err := os.OpenFile(u.journal, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0600)
	if err != nil {
		return
	}
	_ = json.NewEncoder(f).Encode(e)
	_ = f.Close()
}

func (u *usageTracker) rewriteJournalLocked() {
	if u.journal == "" {
		return
	}
	if err := os.MkdirAll(filepath.Dir(u.journal), 0700); err != nil {
		return
	}
	tmp := u.journal + ".tmp"
	f, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
	if err != nil {
		return
	}
	enc := json.NewEncoder(f)
	ok := true
	for _, e := range u.events {
		if enc.Encode(e) != nil {
			ok = false
			break
		}
	}
	if f.Close() != nil {
		ok = false
	}
	if ok {
		_ = os.Rename(tmp, u.journal)
	} else {
		_ = os.Remove(tmp)
	}
}

func (u *usageTracker) loadJournal(now time.Time) {
	if u.journal == "" {
		return
	}
	f, err := os.Open(u.journal)
	if err != nil {
		return
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	starts := make(map[uint64]usageEvent)
	for s.Scan() {
		var e usageEvent
		if json.Unmarshal(s.Bytes(), &e) != nil || e.Seq == 0 || e.EventID == "" {
			continue
		}
		if inst := eventInstance(e.EventID); inst != "" {
			u.instance = inst
		}
		if e.Seq > u.seq {
			u.seq = e.Seq
		}
		u.events = append(u.events, e)
		if e.Type == "session_started" {
			starts[e.SessionID] = e
		} else if e.Type == "session_ended" {
			delete(starts, e.SessionID)
		}
	}
	if len(u.events) > 4096 {
		u.events = append([]usageEvent(nil), u.events[len(u.events)-4096:]...)
	}
	// A process restart destroys live UDP state. Persist a terminal event for
	// every unmatched START so external accounting cannot remain REDEEMED.
	for _, st := range starts {
		u.seq++
		start := time.UnixMilli(st.StartedAtUnixMs)
		if st.StartedAtUnixMs <= 0 {
			start = now
		}
		d := now.Sub(start)
		if d < 0 {
			d = 0
		}
		u.events = append(u.events, usageEvent{Type: "session_ended", EventID: fmt.Sprintf("%s:%d:restart-end", u.instance, st.SessionID), TicketID: st.TicketID, Seq: u.seq, SessionID: st.SessionID, StartedAtUnixMs: start.UnixMilli(), EndedAtUnixMs: now.UnixMilli(), DurationMs: d.Milliseconds(), Reason: "relay_restart"})
	}
	u.rewriteJournalLocked()
}

func (u *usageTracker) start(id uint64, now time.Time) { u.startWithTicket(id, "", now) }
func (u *usageTracker) startWithTicket(id uint64, ticketID string, now time.Time) {
	if id == 0 {
		return
	}
	u.mu.Lock()
	defer u.mu.Unlock()
	if _, ok := u.sessions[id]; !ok {
		u.sessions[id] = &sessionUsage{TicketID: ticketID, CreatedAt: now}
		u.seq++
		u.appendEventLocked(usageEvent{Type: "session_started", EventID: fmt.Sprintf("%s:%d:start", u.instance, id), TicketID: ticketID, Seq: u.seq, SessionID: id, StartedAtUnixMs: now.UnixMilli(), EndedAtUnixMs: now.UnixMilli()})
	}
}
func (u *usageTracker) discard(id uint64) { u.mu.Lock(); delete(u.sessions, id); u.mu.Unlock() }
func (u *usageTracker) gameplayActive(id uint64, now time.Time) {
	u.mu.Lock()
	defer u.mu.Unlock()
	if s := u.sessions[id]; s != nil {
		if s.GameplayStarted.IsZero() {
			s.GameplayStarted = now
		}
		s.LastGameplayAt = now
	}
}
func (u *usageTracker) gameplayIdle(id uint64, now time.Time) {
	u.mu.Lock()
	defer u.mu.Unlock()
	if s := u.sessions[id]; s != nil && !s.GameplayStarted.IsZero() {
		if now.After(s.GameplayStarted) {
			s.GameplayTotal += now.Sub(s.GameplayStarted)
		}
		s.GameplayStarted = time.Time{}
		s.LastGameplayAt = time.Time{}
	}
}

func (u *usageTracker) packetLoss(id uint64, pct float64) {
	if pct < 0 || pct > 100 {
		return
	}
	u.mu.Lock()
	defer u.mu.Unlock()
	if s := u.sessions[id]; s != nil {
		v := pct
		s.PacketLossPct = &v
	}
}

// quality records generic OSS session-quality telemetry. It knows nothing about
// service internals; a local consumer may choose whether to export the event.
func (u *usageTracker) quality(id uint64, report glocore.QualityReport, counters glocore.DataCounters) {
	u.mu.Lock()
	defer u.mu.Unlock()
	s := u.sessions[id]
	if s == nil {
		return
	}
	if report.RelayRTTMicros > 0 && report.RelayRTTMicros <= 10_000_000 {
		s.RelayRTTMsSum += float64(report.RelayRTTMicros) / 1000.0
		s.RelayRTTSamples++
	}
	// Session sequence counters are cumulative. Never let an older/reordered
	// StatsRequest move the aggregate backwards.
	if counters.C2RSeqReceived >= s.C2RSeqReceived && counters.C2RSeqLost >= s.C2RSeqLost &&
		report.S2CSeqReceived >= s.S2CSeqReceived && report.S2CSeqLost >= s.S2CSeqLost {
		s.C2RSeqReceived = counters.C2RSeqReceived
		s.C2RSeqLost = counters.C2RSeqLost
		s.S2CSeqReceived = report.S2CSeqReceived
		s.S2CSeqLost = report.S2CSeqLost
		s.QualityCountersSet = true
	}
}

func (u *usageTracker) addTunnelRX(id uint64, n int) {
	if n > 0 {
		u.add(id, 0, uint64(n))
	}
}
func (u *usageTracker) addTunnelTX(id uint64, n int) {
	if n > 0 {
		u.add(id, 1, uint64(n))
	}
}
func (u *usageTracker) addGameRX(id uint64, n int) {
	if n > 0 {
		u.addGame(id, 2, uint64(n))
	}
}
func (u *usageTracker) addGameTX(id uint64, n int) {
	if n > 0 {
		u.addGame(id, 3, uint64(n))
	}
}
func (u *usageTracker) addGame(id uint64, kind int, n uint64) {
	u.mu.Lock()
	defer u.mu.Unlock()
	s := u.sessions[id]
	if s == nil {
		return
	}
	if s.GameplayStarted.IsZero() {
		s.GameplayStarted = time.Now()
	}
	s.LastGameplayAt = time.Now()
	if kind == 2 {
		s.GameRXBytes += n
	} else {
		s.GameTXBytes += n
	}
}
func (u *usageTracker) expireGameplay(now time.Time, idle time.Duration) {
	if idle <= 0 {
		return
	}
	u.mu.Lock()
	defer u.mu.Unlock()
	for _, s := range u.sessions {
		if !s.GameplayStarted.IsZero() && !s.LastGameplayAt.IsZero() && now.Sub(s.LastGameplayAt) >= idle {
			end := s.LastGameplayAt.Add(idle)
			if end.After(s.GameplayStarted) {
				s.GameplayTotal += end.Sub(s.GameplayStarted)
			}
			s.GameplayStarted = time.Time{}
			s.LastGameplayAt = time.Time{}
		}
	}
}
func (u *usageTracker) add(id uint64, kind int, n uint64) {
	u.mu.Lock()
	defer u.mu.Unlock()
	s := u.sessions[id]
	if s == nil {
		return
	}
	switch kind {
	case 0:
		s.TunnelRXBytes += n
	case 1:
		s.TunnelTXBytes += n
	case 2:
		s.GameRXBytes += n
	case 3:
		s.GameTXBytes += n
	}
}
func (u *usageTracker) finish(id uint64, now time.Time, reason string) bool {
	u.mu.Lock()
	defer u.mu.Unlock()
	s := u.sessions[id]
	if s == nil {
		return false
	}
	if !s.GameplayStarted.IsZero() && now.After(s.GameplayStarted) {
		s.GameplayTotal += now.Sub(s.GameplayStarted)
	}
	d := now.Sub(s.CreatedAt)
	if d < 0 {
		d = 0
	}
	var relayLatency *float64
	if s.RelayRTTSamples > 0 {
		v := s.RelayRTTMsSum / float64(s.RelayRTTSamples)
		relayLatency = &v
	}
	packetLoss := s.PacketLossPct
	if s.QualityCountersSet {
		received := s.C2RSeqReceived + s.S2CSeqReceived
		lost := s.C2RSeqLost + s.S2CSeqLost
		if received+lost > 0 {
			v := 100.0 * float64(lost) / float64(received+lost)
			packetLoss = &v
		}
	}
	u.seq++
	u.appendEventLocked(usageEvent{Type: "session_ended", EventID: fmt.Sprintf("%s:%d:end", u.instance, id), TicketID: s.TicketID, Seq: u.seq, SessionID: id, StartedAtUnixMs: s.CreatedAt.UnixMilli(), EndedAtUnixMs: now.UnixMilli(), DurationMs: d.Milliseconds(), GameplayMs: s.GameplayTotal.Milliseconds(), TunnelRXBytes: s.TunnelRXBytes, TunnelTXBytes: s.TunnelTXBytes, GameRXBytes: s.GameRXBytes, GameTXBytes: s.GameTXBytes, Reason: reason, RelayLatencyMs: relayLatency, PacketLossPct: packetLoss})
	delete(u.sessions, id)
	return true
}
func (u *usageTracker) finishAll(now time.Time, reason string) {
	u.mu.Lock()
	ids := make([]uint64, 0, len(u.sessions))
	for id := range u.sessions {
		ids = append(ids, id)
	}
	u.mu.Unlock()
	for _, id := range ids {
		u.finish(id, now, reason)
	}
}

type usageQuery struct {
	Instance string `json:"instance"`
	After    uint64 `json:"after"`
}
type usageReply struct {
	Instance string       `json:"instance"`
	Latest   uint64       `json:"latest"`
	Active   int          `json:"active"`
	Events   []usageEvent `json:"events"`
}

func (u *usageTracker) snapshot(instance string, after uint64) usageReply {
	u.mu.Lock()
	defer u.mu.Unlock()
	if instance != u.instance {
		after = 0
	}
	events := make([]usageEvent, 0)
	for _, e := range u.events {
		if e.Seq > after {
			events = append(events, e)
		}
	}
	return usageReply{Instance: u.instance, Latest: u.seq, Active: len(u.sessions), Events: events}
}

func (u *usageTracker) serve(path string, stop <-chan struct{}) error {
	if path == "" {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		return err
	}
	_ = os.Remove(path)
	ln, err := net.Listen("unix", path)
	if err != nil {
		return err
	}
	if err = os.Chmod(path, 0600); err != nil {
		ln.Close()
		_ = os.Remove(path)
		return err
	}
	go func() { <-stop; _ = ln.Close(); _ = os.Remove(path) }()
	for {
		c, err := ln.Accept()
		if err != nil {
			select {
			case <-stop:
				return nil
			default:
				return err
			}
		}
		go func(conn net.Conn) {
			defer conn.Close()
			_ = conn.SetDeadline(time.Now().Add(2 * time.Second))
			var q usageQuery
			if json.NewDecoder(bufio.NewReader(conn)).Decode(&q) != nil {
				return
			}
			_ = json.NewEncoder(conn).Encode(u.snapshot(q.Instance, q.After))
		}(c)
	}
}
