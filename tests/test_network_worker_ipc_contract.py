from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
NW=(ROOT/'app/core/src/network_worker.cpp').read_text(encoding='utf-8')
UI=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf-8')

def test_gipc_is_overlapped_full_duplex():
    assert 'PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE|FILE_FLAG_OVERLAPPED' in NW
    assert 'OPEN_EXISTING,FILE_FLAG_OVERLAPPED' in NW and 'CancelIoEx(pipe, &ov)' in NW

def test_control_writes_are_bounded():
    assert 'FlushFileBuffers(pipe)' not in NW and 'kIpcWriteTimeoutMs = 2000' in NW

def test_connect_carries_generic_config_without_provider_roundtrip():
    assert 'session_grant' in NW and 'relay_public_key' in NW and 'timeout_message' in NW
    assert 'BINDING' not in NW and 'SESSION_GRANT' not in NW and 'grant_cv' not in NW

def test_ui_supports_uri_and_manual_config_paths():
    assert 'parse_handoff_uri' in UI and 'self_host_menu' in UI and 'connect_loaded()' in UI
    assert 'Sign in with Google' not in UI and 'Use GLO Network' not in UI
