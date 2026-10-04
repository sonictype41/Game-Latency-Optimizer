from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_control_protocol_has_no_gameplay_packet_types():
    cpp = (ROOT / 'protocol/include/glo/protocol.hpp').read_text(encoding='utf-8')
    go = (ROOT / 'relay/core/glo_core/core.go').read_text(encoding='utf-8')
    assert 'DataC2S =' not in cpp and 'DataS2C =' not in cpp
    assert 'DataC2S PacketType' not in go and 'DataS2C PacketType' not in go
    assert 'kDataMagic' in cpp and "DataMagic = [4]byte{'G', 'L', 'O', 'D'}" in (ROOT / 'relay/core/glo_core/data_wire.go').read_text(encoding='utf-8')


def test_retired_plaintext_glo2_data_harnesses_are_not_shipped():
    retired = {
        'relay_admission.py', 'relay_bandwidth_limit.py', 'relay_integration.py',
        'relay_load.py', 'relay_multisession_gameplay.py', 'relay_shaper.py',
        'relay_stress.py', 'relay_v5_concurrency.py', 'secure_integration.py',
    }
    present = {p.name for p in (ROOT / 'tests').iterdir() if p.is_file()}
    assert retired.isdisjoint(present)


def test_current_ticket_interop_exercises_glod():
    probe = (ROOT / 'tests/secure_cpp_probe.cpp').read_text(encoding='utf-8')
    interop = (ROOT / 'tests/session_ticket_interop.py').read_text(encoding='utf-8')
    assert 'cmd=="D"' in probe and 'cmd=="V"' in probe
    assert "rpc('D 1 1 '" in interop and "rpc('V '" in interop
