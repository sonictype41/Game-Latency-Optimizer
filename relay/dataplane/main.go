package main

import (
	"bufio"
	"encoding/binary"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	buildpolicy "glo.local/glo/relay/buildpolicy"
	"log"
	"net"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	glocore "glo.local/glo/relay/core/glo_core"
	secure "glo.local/glo/secure_transport"
	sessionkey "glo.local/glo/session_key"
)

type cidrList []*net.IPNet

func (c *cidrList) String() string {
	var v []string
	for _, n := range *c {
		v = append(v, n.String())
	}
	return strings.Join(v, ",")
}
func (c *cidrList) Set(s string) error {
	_, n, err := net.ParseCIDR(s)
	if err != nil {
		return err
	}
	*c = append(*c, n)
	return nil
}

type serverLogger struct {
	debug      bool
	mu         sync.Mutex
	last       map[string]time.Time
	suppressed map[string]uint64
}

func newServerLogger(debug bool) *serverLogger {
	return &serverLogger{debug: debug, last: make(map[string]time.Time), suppressed: make(map[string]uint64)}
}

func (l *serverLogger) emit(level, code, format string, args ...any) {
	if level == "DEBUG" && !l.debug {
		return
	}
	log.Printf("%s code=%s %s", level, code, fmt.Sprintf(format, args...))
}

func (l *serverLogger) info(code, format string, args ...any) { l.emit("INFO", code, format, args...) }
func (l *serverLogger) debugf(code, format string, args ...any) {
	l.emit("DEBUG", code, format, args...)
}
func (l *serverLogger) error(code, format string, args ...any) {
	l.emit("ERROR", code, format, args...)
}

func (l *serverLogger) warnRate(code string, interval time.Duration, format string, args ...any) {
	now := time.Now()
	l.mu.Lock()
	if last, ok := l.last[code]; ok && now.Sub(last) < interval {
		l.suppressed[code]++
		l.mu.Unlock()
		return
	}
	suppressed := l.suppressed[code]
	l.suppressed[code] = 0
	l.last[code] = now
	l.mu.Unlock()
	msg := fmt.Sprintf(format, args...)
	if suppressed != 0 {
		msg = fmt.Sprintf("%s suppressed=%d", msg, suppressed)
	}
	l.emit("WARN", code, "%s", msg)
}

func publicIPv4(ip net.IP) bool {
	v := ip.To4()
	if v == nil {
		return false
	}
	if v[0] == 10 || v[0] == 127 || v[0] == 0 || v[0] >= 224 {
		return false
	}
	if v[0] == 172 && v[1] >= 16 && v[1] <= 31 {
		return false
	}
	if v[0] == 192 && v[1] == 168 {
		return false
	}
	if v[0] == 169 && v[1] == 254 {
		return false
	}
	if v[0] == 100 && v[1] >= 64 && v[1] <= 127 {
		return false
	}
	return true
}

func targetAllowed(ip net.IP, allowed cidrList, allowPrivate bool) bool {
	if ip.To4() == nil {
		return false
	}
	if !allowPrivate && !publicIPv4(ip) {
		return false
	}
	for _, n := range allowed {
		if n.Contains(ip) {
			return true
		}
	}
	return false
}

func loadCIDRFile(path string, out *cidrList) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	line := 0
	for s.Scan() {
		line++
		v := strings.TrimSpace(s.Text())
		if v == "" || strings.HasPrefix(v, "#") {
			continue
		}
		if i := strings.IndexByte(v, '#'); i >= 0 {
			v = strings.TrimSpace(v[:i])
		}
		if v == "" {
			continue
		}
		if err := out.Set(v); err != nil {
			return fmt.Errorf("%s:%d: %w", path, line, err)
		}
	}
	return s.Err()
}

func warnFDLimit(logger *serverLogger, recommended uint64) {
	var lim syscall.Rlimit
	if err := syscall.Getrlimit(syscall.RLIMIT_NOFILE, &lim); err != nil {
		logger.warnRate("FD001", time.Hour, "could not read RLIMIT_NOFILE err=%v", err)
		return
	}
	if recommended > 0 && lim.Cur < recommended {
		logger.warnRate("FD001", time.Hour, "low file-descriptor soft limit current=%d hard=%d recommended>=%d; gameplay flows use one UDP fd each", lim.Cur, lim.Max, recommended)
	} else {
		logger.debugf("FD001", "file_descriptor_limit current=%d hard=%d recommended=%d", lim.Cur, lim.Max, recommended)
	}
}

func main() {
	buildInfo := flag.Bool("build-info", false, "print compiled policy as JSON and exit")
	checkConfig := flag.Bool("check-config", false, "validate configuration without reading keys or opening sockets")
	keyPath := flag.String("key-file", "relay.key", "private X25519 relay key file (0600)")
	keygen := flag.String("keygen", "", "create private key at this path and print PUBLIC key; never overwrites")
	listen := flag.String("listen", ":43170", "UDP listen address")
	eventSocket := flag.String("event-socket", "", "local Unix socket for structured relay lifecycle events; empty keeps log-only mode")
	eventJournal := flag.String("event-journal", "", "optional durable JSONL lifecycle-event journal for restart reconciliation")
	issuerKeys := flag.String("issuer-key-file", "issuer.pub", "trusted Ed25519 grant issuer public-key file")
	redeemedJournal := flag.String("redeemed-ticket-journal", "redeemed-tickets.log", "short-lived single-use ticket replay journal")
	pps := flag.Uint("pps", 0, "deprecated alias for --alloc-per-ip-pps; non-zero overrides it")
	helloRetryGlobalPPS := flag.Int("hello-retry-global-pps", 1000, "global cookie-challenge replies/s; 0 disables")
	helloVerifyGlobalPPS := flag.Int("hello-verify-global-pps", 100, "global verified HELLO/X25519 admissions/s; 0 disables")
	helloVerifyPerIPPPS := flag.Int("hello-verify-per-ip-pps", 20, "verified HELLO/X25519 admissions/s per observed source IP; 0 disables")
	helloInflightGlobal := flag.Int("hello-inflight-global", 256, "maximum verified HELLO cryptographic operations in flight; 0 disables")
	helloInflightPerIP := flag.Int("hello-inflight-per-ip", 32, "maximum verified HELLO cryptographic operations in flight per observed source IP; 0 disables")
	helloWorkers := flag.Int("hello-workers", 4, "bounded HELLO worker count; minimum 1")
	helloQueue := flag.Int("hello-queue", 256, "bounded HELLO work queue; minimum 1")
	allocGlobalPPS := flag.Uint("alloc-global-pps", 100, "global control-session allocations/s after crypto verification; 0 disables")
	allocPerIPPPS := flag.Uint("alloc-per-ip-pps", 20, "control-session allocations/s per observed source IP after crypto verification; 0 disables")
	sessionPPS := flag.Uint("session-pps", 5000, "per-session authenticated packet/s limit")
	sessionKbps := flag.Uint64("session-kbps", 20000, "per-session ingress/egress bandwidth cap in kbit/s")
	sessionBurstMs := flag.Uint("session-burst-ms", 250, "per-session ingress token-bucket burst allowance in milliseconds")
	maxSessions := flag.Int("max-sessions", 200, "maximum concurrent active gameplay sessions; connected lobby/control sessions do not consume a slot")
	maxControlSessions := flag.Int("max-control-sessions", 4096, "maximum concurrent control sessions, including connected lobby clients")
	maxControlSessionsPerIP := flag.Int("max-control-sessions-per-ip", 512, "maximum concurrent control sessions per observed source IP (keep high when relay is behind a shared Transit)")
	maxFlowsPerSession := flag.Int("max-flows-per-session", 8, "maximum active UDP game flows per session")
	flowTimeoutSec := flag.Uint("flow-timeout-sec", 15, "idle game-flow timeout in seconds")
	sessionIdleTimeoutSec := flag.Uint("session-idle-timeout-sec", 25, "authenticated client-session liveness timeout in seconds; 0 disables")
	flowOpenGlobalPPS := flag.Uint("flow-open-global-pps", 2000, "global new/replacement game-flow admission attempts/s; 0 disables")
	flowOpenPerIPPPS := flag.Uint("flow-open-per-ip-pps", 500, "new/replacement game-flow admission attempts/s per observed source IP; 0 disables")
	flowOpenPerSessionPPS := flag.Uint("flow-open-per-session-pps", 50, "new/replacement game-flow admission attempts/s per control session; 0 disables")
	maintenance := flag.Bool("maintenance", false, "reject new gameplay sessions with an explicit maintenance reason while allowing already-active gameplay to finish")
	maintenanceFile := flag.String("maintenance-file", "", "optional local marker file; when present, reject new session redemption and new gameplay flows")
	egressBurstMs := flag.Uint("egress-burst-ms", 250, "per-session DATA_S2C burst credit in milliseconds of configured bandwidth")
	egressQueueMs := flag.Uint("egress-queue-ms", 40, "maximum DATA_S2C latency queue beyond available burst credit, in milliseconds")
	egressGlobalQueueBytes := flag.Uint64("egress-global-queue-bytes", 64<<20, "node-wide cap for queued DATA_S2C bytes; 0 disables the global queue guard")
	statsIntervalSec := flag.Uint("stats-interval-sec", 15, "engine stats/log interval in seconds; minimum 1")
	allowPrivate := flag.Bool("allow-private-targets", false, "permit private/loopback targets only when they also match the destination allowlist (tests/private labs)")
	debug := flag.Bool("debug", false, "enable DEBUG logs")
	var extraAllowed cidrList
	clearDefaultAllowlist := flag.Bool("clear-default-allowlist", false, "start with an empty target allowlist instead of the compatibility Roblox defaults")
	allowCIDRFile := flag.String("allow-cidr-file", "", "load additional destination CIDRs from a line-oriented file (# comments allowed)")
	flag.Var(&extraAllowed, "allow-cidr", "additional allowed destination CIDR (repeatable)")
	policy, err := buildpolicy.Load(embeddedPolicy)
	if err != nil {
		log.Fatal("CONFIG001: ", err)
	}
	if err = policy.Apply(flag.CommandLine); err != nil {
		log.Fatal("CONFIG001: ", err)
	}
	if err = buildpolicy.Precheck(flag.CommandLine, os.Args[1:]); err != nil {
		log.Fatal("CONFIG001: ", err)
	}
	flag.Parse()
	if *pps != 0 {
		if !policy.Args["alloc-per-ip-pps"].Enabled {
			log.Fatal("CONFIG001: --pps cannot override disabled --alloc-per-ip-pps")
		}
		*allocPerIPPPS = *pps
	}

	if err = policy.Validate(flag.CommandLine); err != nil {
		log.Fatal("CONFIG001: ", err)
	}
	if *helloWorkers < 1 || *helloQueue < 1 {
		log.Fatal("CONFIG001: invalid worker/queue count")
	}
	if *buildInfo {
		json.NewEncoder(os.Stdout).Encode(policy)
		return
	}
	if _, port, e := net.SplitHostPort(*listen); e != nil {
		log.Fatal("CONFIG001: invalid listen address")
	} else if n, e := strconv.Atoi(port); e != nil || n < 1 || n > 65535 {
		log.Fatal("CONFIG001: invalid listen port")
	}
	if *keyPath == "" || *issuerKeys == "" {
		log.Fatal("CONFIG001: key-file and issuer-key-file are required")
	}
	if *checkConfig {
		fmt.Println("CONFIG_OK")
		return
	}
	if *keygen != "" {
		pub, err := secure.GenerateKey(*keygen)
		if err != nil {
			log.Fatal(err)
		}
		fmt.Println(pub)
		return
	}
	transport, err := secure.LoadKey(*keyPath)
	if err != nil {
		log.Fatalf("CRYPTO001: %v; generate with --keygen relay.key", err)
	}
	transport.SetLimits(secure.Limits{
		RetryGlobalPPS:    *helloRetryGlobalPPS,
		VerifiedGlobalPPS: *helloVerifyGlobalPPS,
		VerifiedPerIPPPS:  *helloVerifyPerIPPPS,
		MaxInflightGlobal: *helloInflightGlobal,
		MaxInflightPerIP:  *helloInflightPerIP,
		MaxRateTrackedIPs: 4096,
	})
	pubKeys, err := sessionkey.LoadPublicKeys(*issuerKeys)
	if err != nil {
		log.Fatalf("KEY001: cannot load issuer keys: %v", err)
	}
	ticketVerifier, err := sessionkey.NewVerifier(pubKeys, transport.PublicKey(), *redeemedJournal, time.Now())
	if err != nil {
		log.Fatalf("KEY002: cannot initialize ticket verifier: %v", err)
	}
	logger := newServerLogger(*debug)
	usage := newUsageTracker(*eventJournal)
	usageStop := make(chan struct{})
	defer close(usageStop)
	defer usage.finishAll(time.Now(), "relay_shutdown")
	if *eventSocket != "" {
		go func() {
			if err := usage.serve(*eventSocket, usageStop); err != nil {
				logger.error("EVENT001", "event socket failed path=%q err=%v", *eventSocket, err)
			}
		}()
	}
	maintenanceActive := func() bool {
		if *maintenance {
			return true
		}
		if *maintenanceFile == "" {
			return false
		}
		_, err := os.Stat(*maintenanceFile)
		return err == nil
	}

	var allowed cidrList
	if !*clearDefaultAllowlist {
		// Compatibility profile only. The v0.5 engine itself is target-agnostic:
		// operators can clear these defaults and supply policy by CLI/file.
		_ = allowed.Set("128.116.0.0/17")
		_ = allowed.Set("103.140.28.0/23")
		_ = allowed.Set("141.193.3.0/24")
		_ = allowed.Set("205.201.62.0/24")
	}
	allowed = append(allowed, extraAllowed...)
	if *allowCIDRFile != "" {
		if err := loadCIDRFile(*allowCIDRFile, &allowed); err != nil {
			logger.error("SRV004", "allowlist file failed path=%q err=%v", *allowCIDRFile, err)
			return
		}
	}
	if len(allowed) == 0 {
		logger.error("SRV005", "empty destination allowlist; supply --allow-cidr/--allow-cidr-file or omit --clear-default-allowlist")
		return
	}

	addr, err := net.ResolveUDPAddr("udp4", *listen)
	if err != nil {
		logger.error("SRV002", "invalid listen address %q: %v", *listen, err)
		return
	}
	conn, err := net.ListenUDP("udp4", addr)
	if err != nil {
		logger.error("SRV003", "UDP listen failed addr=%s err=%v", addr, err)
		return
	}
	defer conn.Close()
	// Absorb short bursts of small game packets/session handshakes without relying on tiny OS defaults.
	_ = conn.SetReadBuffer(4 << 20)
	_ = conn.SetWriteBuffer(4 << 20)

	srv := glocore.NewServer()
	srv.MaxPPSPerIP = 0
	srv.MaxAllocGlobalPPS = uint32(*allocGlobalPPS)
	srv.MaxAllocPerIPPPS = uint32(*allocPerIPPPS)
	srv.MaxSessionPPS = uint32(*sessionPPS)
	srv.MaxSessionKbps = *sessionKbps
	srv.SessionBurst = time.Duration(*sessionBurstMs) * time.Millisecond
	if *maxSessions < 0 {
		*maxSessions = 0
	}
	if *maxControlSessions < 0 {
		*maxControlSessions = 0
	}
	if *maxFlowsPerSession < 0 {
		*maxFlowsPerSession = 0
	}
	if *maxControlSessionsPerIP < 0 {
		*maxControlSessionsPerIP = 0
	}
	srv.MaxGameplaySessions = *maxSessions
	srv.MaxControlSessions = *maxControlSessions
	srv.MaxControlSessionsPerIP = *maxControlSessionsPerIP
	srv.MaxFlowsPerSess = *maxFlowsPerSession
	srv.FlowTimeout = time.Duration(*flowTimeoutSec) * time.Second
	srv.SessionIdleTimeout = time.Duration(*sessionIdleTimeoutSec) * time.Second
	srv.FlowOpenGlobalPPS = uint32(*flowOpenGlobalPPS)
	srv.FlowOpenPerIPPPS = uint32(*flowOpenPerIPPPS)
	srv.FlowOpenPerSessionPPS = uint32(*flowOpenPerSessionPPS)
	srv.TargetPolicy = glocore.TargetPolicy{Allowed: []*net.IPNet(allowed), AllowPrivate: *allowPrivate}
	if *statsIntervalSec == 0 {
		*statsIntervalSec = 1
	}
	queueLimitBytes := queueBytesForRate(*sessionKbps, *egressQueueMs)
	burstBytes := queueBytesForRate(*sessionKbps, *egressBurstMs)
	recommendedFDs := uint64(128)
	if *maxSessions > 0 && *maxFlowsPerSession > 0 {
		recommendedFDs += uint64(*maxSessions) * uint64(*maxFlowsPerSession)
	}
	warnFDLimit(logger, recommendedFDs)
	logger.info("SRV001", "GLO generic relay listening on %s; allow=%s; session_rate=%dkbit/s session_burst=%dms gameplay_capacity=%d control_capacity=%d control_per_ip=%d max_flows=%d session_ttl=signed-grant session_idle_timeout=%ds alloc_global_pps=%d alloc_per_ip_pps=%d hello_verify_global_pps=%d hello_verify_per_ip_pps=%d flow_open_global_pps=%d flow_open_per_ip_pps=%d flow_open_per_session_pps=%d maintenance=%t egress_burst=%dms egress_burst_max=%dB egress_queue=%dms egress_queue_max=%dB global_queue_max=%dB",
		conn.LocalAddr(), allowed.String(), *sessionKbps, *sessionBurstMs, *maxSessions, *maxControlSessions, *maxControlSessionsPerIP, *maxFlowsPerSession, *sessionIdleTimeoutSec, *allocGlobalPPS, *allocPerIPPPS, *helloVerifyGlobalPPS, *helloVerifyPerIPPPS, *flowOpenGlobalPPS, *flowOpenPerIPPPS, *flowOpenPerSessionPPS, *maintenance, *egressBurstMs, burstBytes, *egressQueueMs, queueLimitBytes, *egressGlobalQueueBytes)

	type dataCounters struct {
		tunnelRX atomic.Uint64
		gameTX   atomic.Uint64
		gameRX   atomic.Uint64
		tunnelTX atomic.Uint64
	}
	var counters dataCounters

	sendEncoded := func(peer *net.UDPAddr, b []byte) bool {
		if peer == nil || len(b) == 0 {
			return false
		}
		// net.UDPConn supports concurrent methods. Datagram boundaries are already
		// atomic, so a node-wide send mutex only serialized unrelated sessions.
		_, writeErr := conn.WriteToUDP(b, peer)
		if writeErr != nil {
			logger.warnRate("NET002", 10*time.Second, "tunnel UDP write failed peer=%s err=%v", peer, writeErr)
			return false
		}
		return true
	}
	sendPacket := func(peer *net.UDPAddr, p glocore.Packet) {
		plain, e := glocore.Encode(p)
		var b []byte
		if e == nil {
			b, e = transport.Seal(p.SessionID, plain)
		}
		if e != nil {
			logger.warnRate("PROTO002", 10*time.Second, "internal control encode failed type=%d err=%v", p.Type, e)
			return
		}
		sendEncoded(peer, b)
	}
	flowReject := func(peer *net.UDPAddr, sessionID uint64, sequence uint64, flowID uint32, admission glocore.FlowAdmission) {
		var code glocore.RelayRejectReason
		var reason string
		switch admission {
		case glocore.FlowRejectedCapacity:
			code, reason = glocore.RejectCapacity, "capacity"
		case glocore.FlowRejectedMaintenance:
			code, reason = glocore.RejectMaintenance, "maintenance"
		default:
			code, reason = glocore.RejectTemporary, "temporary"
		}
		sendPacket(peer, glocore.Packet{Type: glocore.Error, Flags: uint16(code), SessionID: sessionID, Nonce: sequence, FlowID: flowID})
		logger.info("RELAY007", "event=gameplay_rejected reason=%s session=%d flow=%d active_gameplay=%d capacity=%d",
			reason, sessionID, flowID, srv.ActiveGameplaySessions(), srv.MaxGameplaySessions)
	}
	shaper := newEgressShaperWithPolicy(*sessionKbps, *egressBurstMs, *egressQueueMs, *egressGlobalQueueBytes, sendEncoded,
		func(sessionID uint64) {
			counters.tunnelTX.Add(1)
			srv.CountRelayToClient(sessionID)
		},
		func(sessionID uint64) bool { return srv.DataSessionEndpoint(sessionID, time.Now()) != nil })
	var dataPool sync.Pool
	dataPool.New = func() any { return make([]byte, 0, glocore.DataMaxDatagram) }
	shaper.SetBufferRelease(func(b []byte) {
		if cap(b) >= glocore.DataMaxDatagram {
			dataPool.Put(b[:0])
		}
	})
	defer shaper.Close()
	sendDataPacket := func(peer *net.UDPAddr, sessionID uint64, sequence uint64, flowID uint32, payload []byte, priorityProof bool) bool {
		if sequence == 0 {
			sequence = srv.NextRelaySequence(sessionID)
			if sequence == 0 {
				return false
			}
		}
		pooled := dataPool.Get().([]byte)[:0]
		b, e := glocore.EncodeDataInto(pooled, glocore.DataFrame{
			Direction: glocore.DataDirectionS2C,
			SessionID: sessionID,
			Sequence:  sequence,
			FlowID:    flowID,
			Payload:   payload,
		})
		if e != nil {
			if cap(pooled) >= glocore.DataMaxDatagram {
				dataPool.Put(pooled[:0])
			}
			logger.warnRate("PROTO002", 10*time.Second, "internal GLOD S2C encode failed err=%v", e)
			return false
		}
		if priorityProof {
			if shaper.EnqueuePriority(sessionID, peer, b) {
				usage.addTunnelTX(sessionID, len(b))
				logger.debugf("FLOW004", "event=first_reverse_priority_queued session=%d flow=%d", sessionID, flowID)
				return true
			}
			logger.warnRate("FLOW005", 5*time.Second, "first reverse priority enqueue failed session=%d flow=%d", sessionID, flowID)
			return false
		}
		ok := shaper.Enqueue(sessionID, peer, b)
		if ok {
			usage.addTunnelTX(sessionID, len(b))
		}
		return ok
	}

	cleanupStop := make(chan struct{})
	defer close(cleanupStop)
	go func() {
		t := time.NewTicker(time.Second)
		defer t.Stop()
		for {
			select {
			case now := <-t.C:
				for _, id := range srv.ExpireIdleSessions(now) {
					transport.Remove(id)
					shaper.RemoveSession(id)
					usage.finish(id, now, "client_timeout")
					logger.info("SESS004", "event=session_closed reason=client_timeout id=%d idle_timeout=%s", id, srv.SessionIdleTimeout)
				}
				usage.expireGameplay(now, srv.FlowTimeout)
				s, f := srv.Cleanup(now)
				if s+f > 0 {
					stats := srv.Stats()
					logger.info("GC001", "cleanup removed_sessions=%d removed_flows=%d control_sessions=%d gameplay_sessions=%d active_flows=%d", s, f, stats.ControlSessions, stats.GameplaySessions, stats.ActiveFlows)
				}
				for _, id := range transport.Cleanup(now, func(id uint64) bool { return srv.SessionEndpoint(id) != nil }) {
					if peer := srv.SessionEndpoint(id); peer != nil {
						srv.RemoveSession(peer, id)
					}
					usage.finish(id, now, "expired")
					shaper.RemoveSession(id)
				}
				shaper.PruneInactive()
			case <-cleanupStop:
				return
			}
		}
	}()

	go func() {
		t := time.NewTicker(time.Duration(*statsIntervalSec) * time.Second)
		defer t.Stop()
		var lastRX, lastGTX, lastGRX, lastTX, lastDrop, lastShapedBytes uint64
		lastSample := time.Now()
		for now := range t.C {
			rx := counters.tunnelRX.Load()
			gtx := counters.gameTX.Load()
			grx := counters.gameRX.Load()
			tx := counters.tunnelTX.Load()
			qbytes, qpeak, qage, drops, shapedBytes := shaper.Stats()
			qp50, qp95, qp99, qsamples := shaper.QueueLatencyInterval()
			elapsed := now.Sub(lastSample).Seconds()
			shapedKbps := 0.0
			if elapsed > 0 && shapedBytes >= lastShapedBytes {
				shapedKbps = float64(shapedBytes-lastShapedBytes) * 8.0 / 1000.0 / elapsed
			}
			stats := srv.Stats()
			logger.info("ENG001", "control_sessions=%d gameplay_sessions=%d active_flows=%d sessions_created=%d sessions_closed=%d sessions_expired=%d flows_opened=%d flows_closed=%d rate_limited_packets=%d rate_limited_bytes=%d session_admission_drops=%d flow_admission_rate_drops=%d",
				stats.ControlSessions, stats.GameplaySessions, stats.ActiveFlows, stats.SessionsCreated, stats.SessionsClosed, stats.SessionsExpired, stats.FlowsOpened, stats.FlowsClosed, stats.RateLimitedPackets, stats.RateLimitedBytes, stats.SessionAdmissionDrops, stats.FlowAdmissionRateDrops)
			if rx != lastRX || gtx != lastGTX || grx != lastGRX || tx != lastTX || drops != lastDrop || qbytes != 0 {
				logger.info("DATA001", "tunnel_rx=%d game_tx=%d game_rx=%d tunnel_tx=%d egress_queue_bytes=%d egress_queue_peak_bytes=%d egress_queue_max_age_ms=%d egress_queue_p50_ms=%d egress_queue_p95_ms=%d egress_queue_p99_ms=%d egress_queue_samples=%d egress_queue_drops=%d egress_shaped_kbit_s=%.1f control_sessions=%d gameplay_sessions=%d active_flows=%d",
					rx, gtx, grx, tx, qbytes, qpeak, qage, qp50, qp95, qp99, qsamples, drops, shapedKbps, stats.ControlSessions, stats.GameplaySessions, stats.ActiveFlows)
				if gtx > lastGTX && grx == lastGRX && gtx-lastGTX >= 8 {
					logger.warnRate("FLOW001", 15*time.Second, "outbound gameplay advanced without reverse traffic delta game_tx=%d game_rx=%d gameplay_sessions=%d", gtx-lastGTX, grx-lastGRX, stats.GameplaySessions)
				}
				if drops > lastDrop {
					logger.warnRate("LIMIT001", 15*time.Second, "relay egress shaper queue overflow dropped=%d during interval", drops-lastDrop)
				}
				lastRX, lastGTX, lastGRX, lastTX, lastDrop = rx, gtx, grx, tx, drops
			}
			lastShapedBytes = shapedBytes
			lastSample = now
		}
	}()

	type helloJob struct {
		peer *net.UDPAddr
		data []byte
		now  time.Time
	}
	helloJobs := make(chan helloJob, *helloQueue)
	var helloWG sync.WaitGroup
	for i := 0; i < *helloWorkers; i++ {
		helloWG.Add(1)
		go func() {
			defer helloWG.Done()
			for job := range helloJobs {
				reply := transport.Handshake(job.peer, job.data, job.now,
					func(rawTicket []byte, now time.Time) (secure.Admission, bool) {
						if maintenanceActive() {
							logger.warnRate("MAINT001", time.Second, "session ticket deferred during maintenance peer=%s", job.peer)
							return secure.Admission{}, false
						}
						ticket, err := ticketVerifier.Verify(rawTicket, now)
						if err != nil {
							logger.warnRate("KEY003", time.Second, "session ticket rejected peer=%s err=%v", job.peer, err)
							return secure.Admission{}, false
						}
						return secure.Admission{TicketID: ticket.ID, SessionTTL: time.Duration(ticket.SessionTTL) * time.Second}, true
					},
					func(ad secure.Admission) (uint64, bool) {
						ticketID := sessionkey.IDHex(ad.TicketID)
						return srv.NewSessionWithGrant(job.peer, job.now, ticketID, ad.SessionTTL)
					},
					func(rawTicket, identity []byte, now time.Time) bool {
						ticket, _, err := sessionkey.Parse(rawTicket)
						if err != nil {
							return false
						}
						if err = ticketVerifier.Redeem(ticket, identity, now); err != nil {
							logger.warnRate("KEY004", time.Second, "session ticket redeem rejected peer=%s err=%v", job.peer, err)
							return false
						}
						return true
					},
					func(id uint64, ad secure.Admission) []byte {
						var ttl [4]byte
						seconds := uint64(ad.SessionTTL / time.Second)
						if seconds > uint64(^uint32(0)) {
							seconds = uint64(^uint32(0))
						}
						binary.BigEndian.PutUint32(ttl[:], uint32(seconds))
						b, _ := glocore.Encode(glocore.Packet{Type: glocore.Welcome, SessionID: id, Payload: ttl[:]})
						return b
					},
					func(id uint64, ad secure.Admission) {
						ticketID := sessionkey.IDHex(ad.TicketID)
						usage.startWithTicket(id, ticketID, job.now)
						logger.info("SESS001", "event=session_started id=%d ticket=%s ttl=%s peer=%s", id, ticketID, ad.SessionTTL, job.peer)
					},
					func(id uint64) { srv.RemoveSession(job.peer, id) })
				if reply != nil {
					sendEncoded(job.peer, reply)
				}
			}
		}()
	}
	defer func() { close(helloJobs); helloWG.Wait() }()

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	go func() { <-stop; _ = conn.Close() }()

	startReverse := func(sid uint64, fid uint32, flow *glocore.Flow, flowConn *net.UDPConn) {
		go func() {
			rb := make([]byte, glocore.MaxInnerUDPPayload+1)
			firstReverse := true
			for {
				rn, err := flowConn.Read(rb)
				if err != nil {
					return
				}
				if rn > glocore.MaxInnerUDPPayload {
					continue
				}
				ep, seq, ok := srv.PrepareS2C(sid, fid, flow, time.Now())
				if !ok {
					return
				}
				counters.gameRX.Add(1)
				usage.addGameRX(sid, rn)
				if firstReverse {
					logger.debugf("FLOW004", "event=first_game_rx session=%d flow=%d bytes=%d", sid, fid, rn)
				}
				if sendDataPacket(ep, sid, seq, fid, rb[:rn], firstReverse) && firstReverse {
					firstReverse = false
				}
			}
		}()
	}

	maxWireDatagram := secure.MaxDatagram
	if glocore.DataMaxDatagram > maxWireDatagram {
		maxWireDatagram = glocore.DataMaxDatagram
	}
	buf := make([]byte, maxWireDatagram+1)
	for {
		n, peer, err := conn.ReadFromUDP(buf)
		if err != nil {
			if errors.Is(err, net.ErrClosed) {
				return
			}
			logger.warnRate("NET001", 10*time.Second, "relay socket read failed err=%v", err)
			continue
		}
		now := time.Now()
		if n > maxWireDatagram {
			continue
		}
		if n >= 4 && buf[0] == 'G' && buf[1] == 'L' && buf[2] == 'H' && buf[3] == '6' {
			// Keep unauthenticated parsing cheap. signed-grant admission accepts the fixed HELLO or
			// the bounded AUTH_HELLO carrying exactly one fixed-size session ticket.
			validHello := n == secure.HelloSize && buf[4] == 1
			validAuth := n == secure.AuthHelloPrefix+sessionkey.TicketSize && buf[4] == 4 && int(buf[104])<<8|int(buf[105]) == sessionkey.TicketSize
			if (!validHello && !validAuth) || buf[5] != 0 || buf[6] != 0 || buf[7] != 0 {
				continue
			}
			peerCopy := *peer
			peerCopy.IP = append(net.IP(nil), peer.IP...)
			job := helloJob{peer: &peerCopy, data: append([]byte(nil), buf[:n]...), now: now}
			select {
			case helloJobs <- job:
			default:
				logger.warnRate("CRYPTO006", 5*time.Second, "HELLO worker queue full queue=%d workers=%d; dropping handshake work", *helloQueue, *helloWorkers)
			}
			continue
		}

		if n >= 4 && buf[0] == 'G' && buf[1] == 'L' && buf[2] == 'O' && buf[3] == 'D' {
			frame, e := glocore.DecodeDataView(buf[:n])
			if e != nil || frame.Direction != glocore.DataDirectionC2S {
				continue
			}
			flow, ok := srv.ValidateDataC2S(peer, frame.SessionID, now, n, frame.Sequence, frame.FlowID)
			if !ok {
				continue
			}
			usage.addTunnelRX(frame.SessionID, n)
			counters.tunnelRX.Add(1)

			prefix := frame.Endpoint
			if flow == nil || !flow.EndpointMatches(prefix) {
				target, tport, cport, e := glocore.DecodeFlowEndpoint(prefix)
				if e != nil || !srv.TargetAllowed(target, tport, cport) {
					continue
				}
				if admission := srv.PreflightFlowOpen(frame.SessionID, frame.FlowID, now, maintenanceActive()); admission != glocore.FlowAccepted {
					flowReject(peer, frame.SessionID, frame.Sequence, frame.FlowID, admission)
					continue
				}
				raddr := &net.UDPAddr{IP: target, Port: int(tport)}
				flowConn, e := net.DialUDP("udp4", nil, raddr)
				if e != nil {
					logger.warnRate("FLOW002", 10*time.Second, "UDP dial failed target=%s err=%v", raddr, e)
					continue
				}
				flow = &glocore.Flow{ID: frame.FlowID, Target: raddr, ClientPort: cport, Conn: flowConn, LastSeen: now}
				if !flow.SetEndpoint(prefix) {
					_ = flowConn.Close()
					continue
				}
				admission, becameActive := srv.AdmitFlowDetailed(frame.SessionID, flow, maintenanceActive())
				if admission != glocore.FlowAccepted {
					_ = flowConn.Close()
					flowReject(peer, frame.SessionID, frame.Sequence, frame.FlowID, admission)
					continue
				}
				if becameActive {
					usage.gameplayActive(frame.SessionID, now)
					logger.info("SESS005", "event=gameplay_active session=%d active_gameplay=%d", frame.SessionID, srv.ActiveGameplaySessions())
				}
				startReverse(frame.SessionID, frame.FlowID, flow, flowConn)
			}
			if flow == nil || !flow.EndpointMatches(prefix) {
				continue
			}
			if written, e := flow.Conn.Write(frame.Payload); e == nil {
				counters.gameTX.Add(1)
				usage.addGameTX(frame.SessionID, written)
				srv.CountRelayToGame(frame.SessionID)
			} else {
				logger.warnRate("FLOW003", 10*time.Second, "game UDP write failed session=%d flow=%d target=%s err=%v", frame.SessionID, frame.FlowID, flow.Target, e)
			}
			continue
		}

		// GLO6 is the encrypted control plane only. Unknown plaintext datagrams
		// are dropped before any cryptographic work.
		if n < 4 || buf[0] != 'G' || buf[1] != 'L' || buf[2] != 'O' || buf[3] != '6' {
			continue
		}
		secureID, plain, valid := transport.Open(peer, buf[:n], now)
		if !valid {
			continue
		}
		p, e := glocore.DecodeView(plain)
		if e != nil || p.SessionID != secureID {
			continue
		}
		if p.Type == glocore.Finish {
			if len(p.Payload) != 0 || p.Flags != glocore.FinishFlagGLOD1 || p.FlowID != 0 {
				continue
			}
			if _, ok := srv.ValidateSession(peer, p.SessionID, now, n); !ok {
				continue
			}
			if !srv.ActivateSession(peer, p.SessionID, now) {
				transport.Remove(p.SessionID)
				continue
			}
			transport.Activate(p.SessionID)
			sendPacket(peer, glocore.Packet{Type: glocore.FinishAck, SessionID: p.SessionID, Flags: glocore.FinishFlagGLOD1})
			continue
		}
		if !transport.Active(p.SessionID) {
			continue
		}
		if _, ok := srv.ValidateSession(peer, p.SessionID, now, n); !ok {
			continue
		}

		switch p.Type {
		case glocore.Ping:
			sendPacket(peer, glocore.Packet{Type: glocore.Pong, SessionID: p.SessionID, Nonce: p.Nonce})
		case glocore.Bye:
			// r9 clients attach a final service-agnostic quality snapshot to Bye.
			// Apply it while DataStats still exists, then close the session.
			if report, err := glocore.DecodeQualityReport(p.Payload); err == nil {
				if stats, ok := srv.DataStats(p.SessionID); ok {
					usage.quality(p.SessionID, report, stats)
				}
			}
			closed := srv.RemoveSession(peer, p.SessionID)
			transport.Remove(p.SessionID)
			shaper.RemoveSession(p.SessionID)
			if closed {
				usage.finish(p.SessionID, now, "client_bye")
				stats := srv.Stats()
				logger.info("SESS003", "event=session_closed reason=client_bye id=%d peer=%s control_sessions=%d gameplay_sessions=%d",
					p.SessionID, peer, stats.ControlSessions, stats.GameplaySessions)
			}
		case glocore.StatsRequest:
			stats, haveStats := srv.DataStats(p.SessionID)
			if report, err := glocore.DecodeQualityReport(p.Payload); err == nil && haveStats {
				usage.quality(p.SessionID, report, stats)
			} else if len(p.Payload) == 4 { // legacy client compatibility
				scaled := binary.BigEndian.Uint32(p.Payload)
				if scaled <= 1000000 {
					usage.packetLoss(p.SessionID, float64(scaled)/10000.0)
				}
			}
			if haveStats {
				sendPacket(peer, glocore.Packet{Type: glocore.StatsResponse, SessionID: p.SessionID, Nonce: p.Nonce, Payload: glocore.EncodeDataCounters(stats)})
			}
		case glocore.FlowClose:
			removed, becameIdle := srv.RemoveFlowDetailed(p.SessionID, p.FlowID)
			if removed && becameIdle {
				usage.gameplayIdle(p.SessionID, now)
				logger.info("SESS006", "event=gameplay_idle session=%d active_gameplay=%d", p.SessionID, srv.ActiveGameplaySessions())
			}
		}
	}
}
