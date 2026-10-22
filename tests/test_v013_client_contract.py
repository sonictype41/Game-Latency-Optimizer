"""v0.1.3 OSS contract: local assets, shared dashboard, native concurrency guard."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def test_v013_version_and_changelog():
    assert (ROOT/'VERSION').read_text(encoding="utf-8").strip() == '0.1.3'
    assert (ROOT/'RELEASE').read_text(encoding="utf-8").strip() == '0.1.3-r1-beta'
    assert '## [0.1.3-beta]' in (ROOT/'CHANGELOG.md').read_text(encoding="utf-8")

def test_v013_pubg_and_shared_dashboard():
    html=(ROOT/'app/frontend/ui/index.html').read_text(encoding="utf-8")
    js=(ROOT/'app/frontend/ui/app.js').read_text(encoding="utf-8")
    assert 'data-game="pubg"' in html
    assert (ROOT/'app/frontend/ui/assets/pubg-presskit.jpg').is_file()
    assert html.count('id="service-stats"')==1
    assert 'id="custom-stats"' not in html
    assert "state.page==='connect'" in js
    assert 'custom-game-placeholder.png' in js
    assert 'showGraph' in js and "c.route==='RELAY'" in js
    assert "send('custom.toggleIp')" in js

def test_v013_native_overlap():
    cpp=(ROOT/'app/frontend/main.cpp').read_text(encoding="utf-8")
    assert 'official_preparing{false}' in cpp
    assert 'another_connection_busy()' in cpp
    assert 'show_overlap_guard()' in cpp
    assert cpp.index('if(another_connection_busy()){show_overlap_guard();return;}') < cpp.index('g_ui->pending_handoff_uri=uri;')
    assert 'g_ui->official_preparing=true' in cpp
    assert 'g_ui->official_preparing=false' in cpp
    assert 'custom.toggleIp' in cpp
