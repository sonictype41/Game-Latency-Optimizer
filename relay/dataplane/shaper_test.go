package main

import (
	"net"
	"sync/atomic"
	"testing"
	"time"
)

func TestQueueBytesForRateUsesMilliseconds(t *testing.T) {
	if got := queueBytesForRate(20000, 200); got != 500000 {
		t.Fatalf("20 Mbit/s * 200 ms: got %d want 500000", got)
	}
	if got := queueBytesForRate(10000, 200); got != 250000 {
		t.Fatalf("10 Mbit/s * 200 ms: got %d want 250000", got)
	}
}

func TestEgressShaperPacesDatagrams(t *testing.T) {
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 43170}
	times := make(chan time.Time, 3)
	s := newEgressShaper(80, 1000,
		func(_ *net.UDPAddr, _ []byte) bool { times <- time.Now(); return true },
		nil, func(uint64) bool { return true })
	defer s.Close()

	payload := make([]byte, 1000) // 80 kbit/s => about 100 ms per datagram.
	for i := 0; i < 3; i++ {
		if !s.Enqueue(1, peer, append([]byte(nil), payload...)) {
			t.Fatalf("enqueue %d failed", i)
		}
	}

	var got [3]time.Time
	for i := range got {
		select {
		case got[i] = <-times:
		case <-time.After(time.Second):
			t.Fatal("timed out waiting for shaped datagram")
		}
	}
	if d := got[1].Sub(got[0]); d < 80*time.Millisecond {
		t.Fatalf("first pacing gap too small: %v", d)
	}
	if d := got[2].Sub(got[1]); d < 80*time.Millisecond {
		t.Fatalf("second pacing gap too small: %v", d)
	}
}

func TestEgressShaperDropsOnlyAfterQueueIsFull(t *testing.T) {
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 43170}
	started := make(chan struct{}, 1)
	release := make(chan struct{})
	s := newEgressShaper(80, 200, // 10 kB/s * 0.2s = exactly 2000 queued bytes.
		func(_ *net.UDPAddr, _ []byte) bool {
			select {
			case started <- struct{}{}:
			default:
			}
			<-release
			return true
		}, nil, func(uint64) bool { return true })
	defer s.Close()

	packet := make([]byte, 1000)
	if !s.Enqueue(7, peer, append([]byte(nil), packet...)) {
		t.Fatal("first enqueue failed")
	}
	select {
	case <-started:
	case <-time.After(time.Second):
		t.Fatal("first datagram did not enter sender")
	}
	if !s.Enqueue(7, peer, append([]byte(nil), packet...)) ||
		!s.Enqueue(7, peer, append([]byte(nil), packet...)) {
		t.Fatal("200 ms queue did not accept its 2000-byte capacity")
	}
	if s.Enqueue(7, peer, append([]byte(nil), packet...)) {
		t.Fatal("queue accepted traffic beyond its 200 ms capacity")
	}
	_, _, _, drops, _ := s.Stats()
	if drops != 1 {
		t.Fatalf("overflow drops=%d want 1", drops)
	}
	close(release)
}

func TestEgressShaperKeepsCreditAfterSchedulerOrSendDelay(t *testing.T) {
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 43170}
	times := make(chan time.Time, 100)
	var sends int
	s := newEgressShaper(800, 1000, // 100 kB/s, 100 B packet => 1 ms virtual spacing.
		func(_ *net.UDPAddr, _ []byte) bool {
			sends++
			times <- time.Now()
			if sends == 1 {
				// Simulate a long scheduler/socket stall. A per-packet scheduler
				// that resets pacing to wall-clock "now" loses this credit and
				// needs another ~99 ms to drain the remaining packets.
				time.Sleep(100 * time.Millisecond)
			}
			return true
		}, nil, func(uint64) bool { return true })
	defer s.Close()

	payload := make([]byte, 100)
	for i := 0; i < 100; i++ {
		if !s.Enqueue(9, peer, append([]byte(nil), payload...)) {
			t.Fatalf("enqueue %d failed", i)
		}
	}

	var first, last time.Time
	for i := 0; i < 100; i++ {
		select {
		case ts := <-times:
			if i == 0 {
				first = ts
			}
			last = ts
		case <-time.After(2 * time.Second):
			t.Fatal("timed out waiting for credit-paced datagrams")
		}
	}
	if elapsed := last.Sub(first); elapsed > 165*time.Millisecond {
		t.Fatalf("virtual-time pacing lost scheduler credit: drain=%v", elapsed)
	}
}

func TestPriorityDatagramDisplacesNormalTrafficButStaysShaped(t *testing.T) {
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 43170}
	started := make(chan int, 8)
	release := make(chan struct{})
	var calls atomic.Int32
	s := newEgressShaper(80, 200,
		func(_ *net.UDPAddr, b []byte) bool {
			call := calls.Add(1)
			started <- len(b)
			if call == 1 {
				<-release
			}
			return true
		}, nil, func(uint64) bool { return true })
	defer s.Close()

	normal := make([]byte, 1000)
	if !s.Enqueue(1, peer, append([]byte(nil), normal...)) {
		t.Fatal("initial enqueue failed")
	}
	select {
	case <-started:
	case <-time.After(time.Second):
		t.Fatal("initial shaped send did not start")
	}
	if !s.Enqueue(1, peer, append([]byte(nil), normal...)) || !s.Enqueue(1, peer, append([]byte(nil), normal...)) {
		t.Fatal("failed to fill queue")
	}
	priority := make([]byte, 64)
	if !s.EnqueuePriority(1, peer, priority) {
		t.Fatal("priority proof was rejected by full queue")
	}
	_, _, _, drops, _ := s.Stats()
	if drops == 0 {
		t.Fatal("priority insertion should evict newest normal traffic when queue is full")
	}
	close(release)
	select {
	case n := <-started:
		if n != len(priority) {
			t.Fatalf("next shaped datagram length=%d want priority %d", n, len(priority))
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatal("priority proof did not move to the head of shaped queue")
	}
}

func TestGlobalQueueGuardBoundsAggregateMemory(t *testing.T) {
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 43170}
	started := make(chan struct{}, 2)
	release := make(chan struct{})
	s := newEgressShaperWithGlobalLimit(80, 1000, 1500,
		func(_ *net.UDPAddr, _ []byte) bool {
			select {
			case started <- struct{}{}:
			default:
			}
			<-release
			return true
		}, nil, func(uint64) bool { return true })
	defer s.Close()

	packet := make([]byte, 1000)
	if !s.Enqueue(1, peer, append([]byte(nil), packet...)) {
		t.Fatal("session 1 initial enqueue failed")
	}
	select {
	case <-started:
	case <-time.After(time.Second):
		t.Fatal("session 1 sender did not start")
	}
	// First datagram is no longer queued while sender is blocked. Session 1 can
	// own 1000 queued bytes, leaving only 500 bytes of the node-wide budget.
	if !s.Enqueue(1, peer, append([]byte(nil), packet...)) {
		t.Fatal("session 1 queue reservation failed")
	}
	if s.Enqueue(2, peer, append([]byte(nil), packet...)) {
		t.Fatal("global queue guard admitted aggregate bytes above limit")
	}
	q, peak, _, drops, _ := s.Stats()
	if q > 1500 || peak > 1500 || drops == 0 {
		t.Fatalf("global guard stats q=%d peak=%d drops=%d", q, peak, drops)
	}
	close(release)
}

func TestEgressBurstCreditSendsShortBurstWithoutPacingDelay(t *testing.T) {
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 43170}
	times := make(chan time.Time, 4)
	// 80 kbit/s = 10kB/s. 250ms burst = 2500 bytes, so two 1000-byte
	// datagrams should leave immediately while the third is still within the
	// 40ms latency queue and must wait for credit to recover.
	s := newEgressShaperWithPolicy(80, 250, 40, 0,
		func(_ *net.UDPAddr, _ []byte) bool { times <- time.Now(); return true },
		nil, func(uint64) bool { return true })
	defer s.Close()
	packet := make([]byte, 1000)
	for i := 0; i < 2; i++ {
		if !s.Enqueue(1, peer, append([]byte(nil), packet...)) {
			t.Fatalf("burst enqueue %d failed", i)
		}
	}
	first := <-times
	second := <-times
	if d := second.Sub(first); d > 25*time.Millisecond {
		t.Fatalf("burst credit did not release short burst promptly: %v", d)
	}
}

func TestQueueLatencyIntervalResets(t *testing.T) {
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 43170}
	done := make(chan struct{}, 1)
	s := newEgressShaperWithPolicy(8000, 0, 40, 0,
		func(_ *net.UDPAddr, _ []byte) bool { done <- struct{}{}; return true },
		nil, func(uint64) bool { return true })
	defer s.Close()
	if !s.Enqueue(1, peer, make([]byte, 100)) {
		t.Fatal("enqueue failed")
	}
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("send timed out")
	}
	p50, p95, p99, samples := s.QueueLatencyInterval()
	if samples != 1 || p50 > queueLatencyOverflowMs || p95 > queueLatencyOverflowMs || p99 > queueLatencyOverflowMs {
		t.Fatalf("bad latency snapshot p50=%d p95=%d p99=%d samples=%d", p50, p95, p99, samples)
	}
	_, _, _, samples = s.QueueLatencyInterval()
	if samples != 0 {
		t.Fatalf("latency histogram did not reset, samples=%d", samples)
	}
}

func TestEgressShaperReleasesOwnedBuffers(t *testing.T) {
	peer := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 43170}
	var released atomic.Int32
	done := make(chan struct{}, 1)
	s := newEgressShaperWithPolicy(8000, 0, 40, 0,
		func(_ *net.UDPAddr, _ []byte) bool { done <- struct{}{}; return true },
		nil, func(uint64) bool { return true })
	s.SetBufferRelease(func([]byte) { released.Add(1) })
	defer s.Close()
	if !s.Enqueue(1, peer, make([]byte, 100)) {
		t.Fatal("enqueue failed")
	}
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("send timed out")
	}
	deadline := time.Now().Add(time.Second)
	for released.Load() != 1 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if got := released.Load(); got != 1 {
		t.Fatalf("released=%d want=1", got)
	}

	// Rejection also consumes ownership and must release exactly once.
	if s.Enqueue(0, peer, make([]byte, 10)) {
		t.Fatal("invalid session unexpectedly enqueued")
	}
	if got := released.Load(); got != 2 {
		t.Fatalf("rejected buffer released=%d want=2", got)
	}
}
