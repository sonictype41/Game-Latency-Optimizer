#!/usr/bin/env python3
"""Bind a generated build manifest to its executable by SHA-256."""
import argparse, hashlib, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('exe',type=Path); ap.add_argument('manifest',type=Path); o=ap.parse_args()
    meta=json.loads(o.manifest.read_text(encoding='utf-8'))
    version=(ROOT/'VERSION').read_text(encoding='utf-8').strip(); release=(ROOT/'RELEASE').read_text(encoding='utf-8').strip()
    if meta.get('version')!=version or meta.get('release')!=release or meta.get('client')!='generic-config' or meta.get('build_id')!='generic-config-client':
        raise SystemExit('generated build identity mismatch')
    meta['exe_sha256']=hashlib.sha256(o.exe.read_bytes()).hexdigest()
    o.manifest.write_text(json.dumps(meta,separators=(',',':'))+'\n',encoding='utf-8',newline='\n')
if __name__=='__main__': main()
