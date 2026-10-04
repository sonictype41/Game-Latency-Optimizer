#!/usr/bin/env python3
import subprocess,sys,tempfile,shutil,json
from pathlib import Path
root=Path(__file__).resolve().parents[1]; build=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as td:
 d=Path(td); b=d/'build'; b.mkdir()
 source_exe=build/'GLO.exe' if (build/'GLO.exe').is_file() else build/'glo-app-windows-amd64.exe'
 source_meta=build/'GLO.build.json' if (build/'GLO.build.json').is_file() else build/'glo-app-windows-amd64.build.json'
 shutil.copyfile(source_exe,b/'GLO.exe'); shutil.copyfile(source_meta,b/'GLO.build.json')
 def run(out,ok):
  p=subprocess.run([sys.executable,str(root/'tools/package_windows.py'),str(b),str(out)],capture_output=True,text=True); assert (p.returncode==0)==ok,(p.stdout,p.stderr)
 run(d/'generic',True); assert (d/'generic/GLO.exe').is_file() and (d/'generic/wintun.dll').is_file(); run(d/'generic',False)
 p=b/'GLO.build.json'; original=p.read_text(encoding='utf-8'); m=json.loads(original); m['client']='managed'; p.write_text(json.dumps(m)); run(d/'bad-manifest',False); p.write_text(original)
 exe=b/'GLO.exe'; original_exe=exe.read_bytes(); exe.write_bytes(original_exe+b'X'); run(d/'tampered-exe',False); exe.write_bytes(original_exe)
print('PASS canonical Windows staging; stale output, wrong identity and tampered executable rejected')
