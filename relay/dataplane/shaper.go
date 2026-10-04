package main

import (
	"net"
	"sync"
	"sync/atomic"
	"time"
)

const queueLatencyOverflowMs = 256

type shapedDatagram struct {
	peer     net.UDPAddr
	bytes    []byte
	enqueued time.Time
}

type sessionEgressShaper struct {
	id       uint64
	owner    *egressShaper
	mu       sync.Mutex
	cond     *sync.Cond
	queue    []shapedDatagram
	bytes    uint64
	nextSend time.Time // virtual serialization timeline; may trail wall time by burst credit
	closed   bool
	stopCh   chan struct{}
}

type egressShaper struct {
	mu                    sync.Mutex
	sessions              map[uint64]*sessionEgressShaper
	rateBytesPerSecond    uint64
	queueDelayLimitBytes  uint64
	burstBytes            uint64
	burstDuration         time.Duration
	globalQueueLimitBytes uint64
	send                  func(*net.UDPAddr, []byte) bool
	onSent                func(uint64)
	alive                 func(uint64) bool
	release               func([]byte)

	queueBytes atomic.Uint64
	queuePeak  atomic.Uint64
	maxAgeMs   atomic.Uint64
	drops      atomic.Uint64
	sentBytes  atomic.Uint64
	latency    [queueLatencyOverflowMs + 1]atomic.Uint64 // 0..255ms, 256=overflow
}

func queueBytesForRate(sessionKbps uint64, queueMs uint) uint64 {
	if sessionKbps == 0 || queueMs == 0 {
		return 0
	}
	// kbit/s * ms / 8 = bytes. This is exactly rate(bytes/s) * queue_ms/1000.
	if sessionKbps > ^uint64(0)/uint64(queueMs) {
		return ^uint64(0)
	}
	return sessionKbps * uint64(queueMs) / 8
}

// Historical constructors intentionally keep burst credit disabled so existing
// unit tests and callers retain their old pacing semantics.
func newEgressShaper(sessionKbps uint64, queueMs uint,
	send func(*net.UDPAddr, []byte) bool,
	onSent func(uint64), alive func(uint64) bool) *egressShaper {
	return newEgressShaperWithPolicy(sessionKbps, 0, queueMs, 0, send, onSent, alive)
}

func newEgressShaperWithGlobalLimit(sessionKbps uint64, queueMs uint, globalQueueLimitBytes uint64,
	send func(*net.UDPAddr, []byte) bool,
	onSent func(uint64), alive func(uint64) bool) *egressShaper {
	return newEgressShaperWithPolicy(sessionKbps, 0, queueMs, globalQueueLimitBytes, send, onSent, alive)
}

func newEgressShaperWithPolicy(sessionKbps uint64, burstMs uint, queueMs uint, globalQueueLimitBytes uint64,
	send func(*net.UDPAddr, []byte) bool,
	onSent func(uint64), alive func(uint64) bool) *egressShaper {
	rate := sessionKbps * 1000 / 8
	return &egressShaper{
		sessions:              make(map[uint64]*sessionEgressShaper),
		rateBytesPerSecond:    rate,
		queueDelayLimitBytes:  queueBytesForRate(sessionKbps, queueMs),
		burstBytes:            queueBytesForRate(sessionKbps, burstMs),
		burstDuration:         time.Duration(burstMs) * time.Millisecond,
		globalQueueLimitBytes: globalQueueLimitBytes,
		send:                  send,
		onSent:                onSent,
		alive:                 alive,
	}
}

func (e *egressShaper) SetBufferRelease(fn func([]byte)) { e.release = fn }
func (e *egressShaper) releaseBuffer(b []byte) {
	if e.release != nil && b != nil {
		e.release(b)
	}
}

func (e *egressShaper) updatePeak(v uint64) {
	for {
		old := e.queuePeak.Load()
		if v <= old || e.queuePeak.CompareAndSwap(old, v) {
			return
		}
	}
}

func (e *egressShaper) updateMaxAge(v uint64) {
	for {
		old := e.maxAgeMs.Load()
		if v <= old || e.maxAgeMs.CompareAndSwap(old, v) {
			return
		}
	}
}

func (e *egressShaper) recordLatency(age time.Duration) {
	if age < 0 {
		age = 0
	}
	ms := int(age / time.Millisecond)
	if ms > queueLatencyOverflowMs {
		ms = queueLatencyOverflowMs
	}
	e.latency[ms].Add(1)
}

func percentileFromBuckets(counts []uint64, total uint64, pct uint64) uint64 {
	if total == 0 {
		return 0
	}
	target := (total*pct + 99) / 100
	var seen uint64
	for i, n := range counts {
		seen += n
		if seen >= target {
			return uint64(i)
		}
	}
	return queueLatencyOverflowMs
}

// QueueLatencyInterval atomically snapshots and resets the queue-age histogram.
// It is designed to be called once per engine stats interval.
func (e *egressShaper) QueueLatencyInterval() (p50, p95, p99, samples uint64) {
	counts := make([]uint64, len(e.latency))
	for i := range e.latency {
		counts[i] = e.latency[i].Swap(0)
		samples += counts[i]
	}
	return percentileFromBuckets(counts, samples, 50), percentileFromBuckets(counts, samples, 95), percentileFromBuckets(counts, samples, 99), samples
}

func (e *egressShaper) reserveQueueBytes(v uint64) bool {
	if v == 0 {
		return true
	}
	for {
		old := e.queueBytes.Load()
		if e.globalQueueLimitBytes > 0 && (old > e.globalQueueLimitBytes || v > e.globalQueueLimitBytes-old) {
			return false
		}
		next := old + v
		if e.queueBytes.CompareAndSwap(old, next) {
			e.updatePeak(next)
			return true
		}
	}
}

func (e *egressShaper) subQueueBytes(v uint64) {
	for v != 0 {
		old := e.queueBytes.Load()
		if old == 0 {
			return
		}
		next := uint64(0)
		if old > v {
			next = old - v
		}
		if e.queueBytes.CompareAndSwap(old, next) {
			return
		}
	}
}

func (e *egressShaper) getOrCreate(sessionID uint64) *sessionEgressShaper {
	e.mu.Lock()
	defer e.mu.Unlock()
	if s := e.sessions[sessionID]; s != nil {
		return s
	}
	s := &sessionEgressShaper{id: sessionID, owner: e, stopCh: make(chan struct{})}
	s.cond = sync.NewCond(&s.mu)
	e.sessions[sessionID] = s
	go s.run()
	return s
}

func durationBytes(rate uint64, d time.Duration) uint64 {
	if rate == 0 || d <= 0 {
		return 0
	}
	secs := uint64(d / time.Second)
	rem := uint64(d % time.Second)
	if secs != 0 && rate > ^uint64(0)/secs {
		return ^uint64(0)
	}
	whole := rate * secs
	if rem != 0 && rate > ^uint64(0)/rem {
		return ^uint64(0)
	}
	frac := rate * rem / uint64(time.Second)
	if whole > ^uint64(0)-frac {
		return ^uint64(0)
	}
	return whole + frac
}

// queueAllowanceLocked returns burst credit currently available plus the true
// latency queue budget. This prevents a 250ms burst allowance from becoming a
// 250ms standing queue after the credit has already been consumed.
func (s *sessionEgressShaper) queueAllowanceLocked(now time.Time, datagramBytes uint64) uint64 {
	e := s.owner
	credit := e.burstBytes
	if e.burstDuration == 0 {
		credit = 0
	} else if !s.nextSend.IsZero() {
		if lag := now.Sub(s.nextSend); lag > 0 {
			if lag > e.burstDuration {
				lag = e.burstDuration
			}
			credit = durationBytes(e.rateBytesPerSecond, lag)
		} else {
			credit = 0
		}
	}
	allowance := credit
	if allowance > ^uint64(0)-e.queueDelayLimitBytes {
		allowance = ^uint64(0)
	} else {
		allowance += e.queueDelayLimitBytes
	}
	if allowance < datagramBytes {
		allowance = datagramBytes // never make a legal datagram impossible to send
	}
	return allowance
}

func (s *sessionEgressShaper) canQueueLocked(now time.Time, add uint64) bool {
	allowance := s.queueAllowanceLocked(now, add)
	return s.bytes <= allowance && add <= allowance-s.bytes
}

func (e *egressShaper) EnqueuePriority(sessionID uint64, peer *net.UDPAddr, b []byte) bool {
	// Ownership of b transfers to the shaper on every call, successful or not.
	if sessionID == 0 || peer == nil || len(b) == 0 {
		e.releaseBuffer(b)
		return false
	}
	if e.rateBytesPerSecond == 0 {
		return e.Enqueue(sessionID, peer, b)
	}
	if e.alive != nil && !e.alive(sessionID) {
		e.releaseBuffer(b)
		return false
	}

	s := e.getOrCreate(sessionID)
	copyPeer := *peer
	now := time.Now()
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		e.releaseBuffer(b)
		return false
	}
	// Verification proof may evict newest normal gameplay datagrams, but it is
	// still constrained by currently available burst credit + latency queue.
	for !s.canQueueLocked(now, uint64(len(b))) && len(s.queue) != 0 {
		last := len(s.queue) - 1
		evictedBuf := s.queue[last].bytes
		evicted := uint64(len(evictedBuf))
		s.queue[last] = shapedDatagram{}
		s.queue = s.queue[:last]
		s.bytes -= evicted
		e.subQueueBytes(evicted)
		e.drops.Add(1)
		e.releaseBuffer(evictedBuf)
	}
	if !s.canQueueLocked(now, uint64(len(b))) {
		s.mu.Unlock()
		e.drops.Add(1)
		e.releaseBuffer(b)
		return false
	}
	reserved := e.reserveQueueBytes(uint64(len(b)))
	for !reserved && len(s.queue) != 0 {
		last := len(s.queue) - 1
		evictedBuf := s.queue[last].bytes
		evicted := uint64(len(evictedBuf))
		s.queue[last] = shapedDatagram{}
		s.queue = s.queue[:last]
		s.bytes -= evicted
		e.subQueueBytes(evicted)
		e.drops.Add(1)
		e.releaseBuffer(evictedBuf)
		reserved = e.reserveQueueBytes(uint64(len(b)))
	}
	if !reserved {
		s.mu.Unlock()
		e.drops.Add(1)
		e.releaseBuffer(b)
		return false
	}
	item := shapedDatagram{peer: copyPeer, bytes: b, enqueued: now}
	s.queue = append(s.queue, shapedDatagram{})
	copy(s.queue[1:], s.queue[:len(s.queue)-1])
	s.queue[0] = item
	s.bytes += uint64(len(b))
	s.cond.Signal()
	s.mu.Unlock()
	return true
}

func (e *egressShaper) Enqueue(sessionID uint64, peer *net.UDPAddr, b []byte) bool {
	// Ownership of b transfers to the shaper on every call, successful or not.
	if sessionID == 0 || peer == nil || len(b) == 0 {
		e.releaseBuffer(b)
		return false
	}
	if e.rateBytesPerSecond == 0 {
		ok := false
		if e.alive == nil || e.alive(sessionID) {
			if e.send != nil && e.send(peer, b) {
				e.sentBytes.Add(uint64(len(b)))
				if e.onSent != nil {
					e.onSent(sessionID)
				}
				ok = true
			}
		}
		e.releaseBuffer(b)
		return ok
	}
	if e.alive != nil && !e.alive(sessionID) {
		e.releaseBuffer(b)
		return false
	}

	s := e.getOrCreate(sessionID)
	copyPeer := *peer
	now := time.Now()
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		e.releaseBuffer(b)
		return false
	}
	if !s.canQueueLocked(now, uint64(len(b))) || !e.reserveQueueBytes(uint64(len(b))) {
		s.mu.Unlock()
		e.drops.Add(1)
		e.releaseBuffer(b)
		return false
	}
	s.queue = append(s.queue, shapedDatagram{peer: copyPeer, bytes: b, enqueued: now})
	s.bytes += uint64(len(b))
	s.cond.Signal()
	s.mu.Unlock()
	return true
}

func stopAndDrainTimer(t *time.Timer) {
	if !t.Stop() {
		select {
		case <-t.C:
		default:
		}
	}
}

func (s *sessionEgressShaper) run() {
	// One reusable timer per session avoids timer allocation churn on every
	// paced datagram. nextSend is a virtual serialization timeline: allowing it
	// to trail wall time by at most burstDuration implements bounded burst credit.
	timer := time.NewTimer(time.Hour)
	stopAndDrainTimer(timer)
	defer timer.Stop()
	for {
		s.mu.Lock()
		wokeFromIdle := false
		for len(s.queue) == 0 && !s.closed {
			wokeFromIdle = true
			s.cond.Wait()
		}
		if s.closed {
			left := s.bytes
			queued := s.queue
			s.queue = nil
			s.bytes = 0
			s.mu.Unlock()
			if left != 0 {
				s.owner.subQueueBytes(left)
			}
			for i := range queued {
				s.owner.releaseBuffer(queued[i].bytes)
			}
			return
		}

		now := time.Now()
		floor := now.Add(-s.owner.burstDuration)
		if s.nextSend.IsZero() {
			s.nextSend = floor
		} else if wokeFromIdle && s.nextSend.Before(floor) {
			// Idle time may refill burst credit, but scheduler/socket delay while a
			// queue is continuously active remains usable pacing credit.
			s.nextSend = floor
		}
		if s.nextSend.After(now) {
			wait := s.nextSend.Sub(now)
			s.mu.Unlock()
			stopAndDrainTimer(timer)
			timer.Reset(wait)
			select {
			case <-timer.C:
			case <-s.stopCh:
				stopAndDrainTimer(timer)
			}
			continue
		}

		item := s.queue[0]
		s.queue[0] = shapedDatagram{}
		s.queue = s.queue[1:]
		s.bytes -= uint64(len(item.bytes))
		bytes := uint64(len(item.bytes))
		spacing := time.Duration((bytes*uint64(time.Second) + s.owner.rateBytesPerSecond - 1) /
			s.owner.rateBytesPerSecond)
		s.nextSend = s.nextSend.Add(spacing)
		s.mu.Unlock()
		s.owner.subQueueBytes(bytes)

		age := time.Since(item.enqueued)
		if age < 0 {
			age = 0
		}
		s.owner.updateMaxAge(uint64(age / time.Millisecond))
		s.owner.recordLatency(age)
		if s.owner.alive == nil || s.owner.alive(s.id) {
			if s.owner.send != nil && s.owner.send(&item.peer, item.bytes) {
				s.owner.sentBytes.Add(bytes)
				if s.owner.onSent != nil {
					s.owner.onSent(s.id)
				}
			}
		}
		s.owner.releaseBuffer(item.bytes)
	}
}

func (e *egressShaper) RemoveSession(sessionID uint64) {
	e.mu.Lock()
	s := e.sessions[sessionID]
	delete(e.sessions, sessionID)
	e.mu.Unlock()
	if s == nil {
		return
	}
	s.mu.Lock()
	if !s.closed {
		s.closed = true
		close(s.stopCh)
		s.cond.Broadcast()
	}
	s.mu.Unlock()
}

func (e *egressShaper) PruneInactive() {
	if e.alive == nil {
		return
	}
	e.mu.Lock()
	ids := make([]uint64, 0, len(e.sessions))
	for id := range e.sessions {
		ids = append(ids, id)
	}
	e.mu.Unlock()
	for _, id := range ids {
		if !e.alive(id) {
			e.RemoveSession(id)
		}
	}
}

func (e *egressShaper) Close() {
	e.mu.Lock()
	ids := make([]uint64, 0, len(e.sessions))
	for id := range e.sessions {
		ids = append(ids, id)
	}
	e.mu.Unlock()
	for _, id := range ids {
		e.RemoveSession(id)
	}
}

func (e *egressShaper) Stats() (queueBytes, peakBytes, maxAgeMs, drops, sentBytes uint64) {
	return e.queueBytes.Load(), e.queuePeak.Load(), e.maxAgeMs.Load(), e.drops.Load(), e.sentBytes.Load()
}
