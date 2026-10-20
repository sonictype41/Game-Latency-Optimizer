#!/usr/bin/env python3
"""Fetch a pinned, official Microsoft WebView2 SDK package for Windows builds.

Headers/loader are BUILD inputs. The browser Runtime is never bundled.
"""
import argparse
import os
import tempfile
import urllib.request
import zipfile
from pathlib import Path

VERSION = "1.0.2739.15"
URL = f"https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/{VERSION}/microsoft.web.webview2.{VERSION}.nupkg"
ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / "third_party" / "webview2" / VERSION

def verify(path):
    return (path / 'build/native/include/WebView2.h').is_file() and (path / 'runtimes/win-x64/native/WebView2Loader.dll').is_file()

def fetch(path):
    if verify(path):
        print(path)
        return path
    path.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        archive = Path(temp) / 'webview2.nupkg'
        with urllib.request.urlopen(URL,timeout=50) as stream, archive.open('wb') as fd:
            import shutil
            shutil.copyfileobj(stream,fd)
        with zipfile.ZipFile(archive) as z:
            for member in z.infolist():
                dest=(path / member.filename).resolve()
                if not dest.is_relative_to(path.resolve()): raise ValueError('Unsafe NuGet archive path')
            z.extractall(path)
    if not verify(path):raise RuntimeError('WebView2 SDK or x64 loader missing from NuGet package')
    print(path)
    return path
if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--destination',type=Path,default=DEFAULT)
    args=parser.parse_args()
    fetch(args.destination)
