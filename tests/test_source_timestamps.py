"""Regression for future-dated source -> Ninja regeneration loop (real CMake/Ninja)."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(shutil.which('cmake') and shutil.which('ninja'), 'CMake/Ninja required')
class TimestampTests(unittest.TestCase):
    def test_detect_repair_and_noop(self):
        with tempfile.TemporaryDirectory(prefix='glo timestamp space ') as td:
            root=Path(td);src=root/'source';build=root/'build';(src/'cmake').mkdir(parents=True)
            guard=Path(__file__).resolve().parents[1]/'cmake/SourceTimestamps.cmake'
            shutil.copyfile(guard,src/'cmake/SourceTimestamps.cmake')
            header=src/'app/core/include/glo/cli_policy.hpp';header.parent.mkdir(parents=True)
            header.write_text('// fixture: content must not change\n')
            (src/'VERSION').write_text('0.12.2\n')
            (src/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.21)
include(cmake/SourceTimestamps.cmake)
project(timestamp_regression NONE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app/core/include/glo/cli_policy.hpp")
''')
            def call(*args,ok=True,env=None):
                p=subprocess.run(args,cwd=src,env=env,text=True,capture_output=True,timeout=20)
                self.assertEqual(p.returncode==0,ok,p.stdout+p.stderr)
                return p.stdout+p.stderr
            call('cmake','-S',str(src),'-B',str(build),'-G','Ninja')
            call('cmake','--build',str(build))
            digest=hashlib.sha256(header.read_bytes()).hexdigest()
            untouched=(src/'VERSION').stat().st_mtime_ns
            future=time.time()+3600;os.utime(header,(future,future))
            output=call('cmake','--build',str(build),ok=False)
            self.assertIn('GLO_FUTURE_SOURCE_TIMESTAMP',output)
            self.assertIn('cli_policy.hpp',output)
            self.assertNotIn('100 tries',output)
            call('cmake','-S',str(src),'-B',str(build),ok=False)
            call('cmake','-DGLO_FIX_SOURCE_TIMESTAMPS=ON','-P','cmake/SourceTimestamps.cmake')
            self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(),digest)
            self.assertEqual((src/'VERSION').stat().st_mtime_ns,untouched)
            call('cmake','-S',str(src),'-B',str(build))
            self.assertIn('no work to do',call('cmake','--build',str(build)))
            # A fixed reproducibility epoch must not be mistaken for wall-clock time.
            env=dict(os.environ,SOURCE_DATE_EPOCH='946684800')
            call('cmake','-S',str(src),'-B',str(build),env=env)
            self.assertIn('no work to do',call('cmake','--build',str(build)))
            # Repair cannot be enabled silently via a persistent cache flag.
            os.utime(header,(future,future))
            call('cmake','-S',str(src),'-B',str(build),'-DGLO_FIX_SOURCE_TIMESTAMPS=ON',ok=False)

if __name__=='__main__':unittest.main(verbosity=2)
