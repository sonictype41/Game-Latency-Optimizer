from pathlib import Path
core=Path('app/core/src/client_core.cpp').read_text(encoding='utf-8'); ui=Path('app/frontend/main.cpp').read_text(encoding='utf-8'); cli=Path('app/core/include/glo/cli_policy.hpp').read_text(encoding='utf-8'); relay=Path('relay/dataplane/main.go').read_text(encoding='utf-8')
checks={
 'snapshot callback dedupe present':'same_snapshot(snapshot_, *last_published_)' in core,
 'generic UI exposes review-first config actions':'Paste JSON config' in ui and 'Import JSON file' in ui and 'Ready to optimize' in ui and 'Review the game and relay, then connect.' in ui,
 'generic UI contains no auth flow':'access_token' not in ui and 'Sign in with Google' not in ui,
 'session TTL comes from relay welcome':'transport.remaining_seconds()' in core,
 'debug CLI retained':'--debug' in cli,
 'server structured logs retained':'SRV001' in relay and 'RELAY007' in relay,
 'quality telemetry retained':'SequenceLossTracker' in core and 'gameplay_ping_ms' in core,
}
failed=[]
for name,ok in checks.items(): print(('PASS' if ok else 'FAIL'),name); failed += [] if ok else [name]
if failed: raise SystemExit('stabilization source checks failed: '+', '.join(failed))
