from pathlib import Path
R=Path(__file__).resolve().parents[1]

def test_art_services_only_and_no_core_modifications():
    j=(R/'app/frontend/ui/app.js').read_text(encoding='utf-8')
    api=(R/'app/frontend/official_api.cpp').read_text(encoding='utf-8')
    html=(R/'app/frontend/ui/index.html').read_text(encoding='utf-8')
    assert 'id="art-enabled"' in html
    assert "routing_preference:state.artEnabled?'advanced':'standard'" in j
    assert 'artFactor:null' in j
    assert 'art_enabled' in j and 'art-session-pill' in j
    assert 'routing_preference' in api and 'cached_art_time_factor_' in api
    assert (R/'VERSION').read_text(encoding='utf-8').strip()=='0.1.5'
    assert (R/'RELEASE').read_text(encoding='utf-8').strip()=='0.1.5-beta'

def test_no_art_dependency_in_engine():
    # UI-only ART state, not the OSS packet/relay engine or wire protocol.
    for path in [R/'app/core/src/client_core.cpp']:
        data=path.read_text(encoding='utf-8')
        assert 'art_route_id' not in data and 'routing_preference' not in data
