# INPUT: Synthetic output streams and a shell-only fake runtime.
# OUTPUT: Bounded normal logs and explicit-only debug/capture assertions.
# POS: Production launch policy regression; never starts an ARM64 game or driver.
import importlib.util,io,os,subprocess,tempfile,unittest
from pathlib import Path
LINUX=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('d2r_log_errors',LINUX/'log-errors.py');sink=importlib.util.module_from_spec(spec);spec.loader.exec_module(sink)

class ProductionLoggingTests(unittest.TestCase):
    def test_normal_output_is_quiet_and_error_tail_is_bounded(self):
        target=io.BytesIO();self.assertFalse(sink.consume(io.StringIO('initialized\nframe ok\n'),target))
        self.assertEqual(target.getvalue(),b'')
        source=io.StringIO(('ERROR synthetic failure\n  detail\n'*8000)+'D2R_LAUNCH_EXIT status=3\n')
        target=io.BytesIO();sink.consume(source,target)
        self.assertLessEqual(len(target.getvalue()),65536)
        self.assertTrue(target.getvalue().endswith(b'D2R_LAUNCH_EXIT status=3\n'))
    def test_vulkan_stop_preserves_the_failed_operation(self):
        line='[nvn-vulkan] STOP vkQueueSubmit (-4)\n'
        target=io.BytesIO();sink.consume(io.StringIO(line),target)
        self.assertEqual(target.getvalue(),line.encode())
    def test_explicit_diagnostics_retains_the_full_stream(self):
        source='initialization\nframe ok\n';target=io.BytesIO()
        sink.consume(io.StringIO(source),target,True);self.assertEqual(target.getvalue(),source.encode())
    def test_failed_sink_still_drains_the_stream(self):
        class Failed:
            def write(self,_): raise OSError('synthetic full disk')
        source=io.StringIO('ERROR one\nERROR two\n')
        self.assertTrue(sink.consume(source,Failed()))
        self.assertEqual(source.tell(),len(source.getvalue()))
    def test_debug_flags_and_frames_require_explicit_diagnostics(self):
        for diagnostics in ('0','1'):
            with self.subTest(diagnostics=diagnostics),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);runtime=root/'runtime';runtime.mkdir();game=root/'game';game.mkdir()
                data=game/'Update/RomFS/data';data.mkdir(parents=True);(data/'build_manifest.psv').write_text('synthetic')
                patches=root/'native';patches.mkdir()
                for name in ('main','sdk','subsdk0'):(patches/(name+'.patched.bin')).write_bytes(b'synthetic')
                shader=root/'shaders';shader.mkdir();(shader/'shader_index.bin').write_bytes(b'synthetic')
                runner=runtime/'run-bringup.sh';runner.write_bytes((LINUX/'run-bringup.sh').read_bytes())
                binary=runtime/'d2r-linux-novideo';binary.write_text('#!/bin/sh\nprintf "%s|%s|%s" "${DM_DBG-unset}" "${DM_SVC-unset}" "${DM_CAPTURE_FRAMES-unset}" > "$D2R_TEST_RECORD"\n');binary.chmod(0o755)
                record=root/'flags.txt';temporary_root=root/'tmp';temporary_root.mkdir()
                env={'PATH':os.defpath,'TMPDIR':str(temporary_root),'D2R_DIAGNOSTICS':diagnostics,'D2R_CAPTURE_FRAMES':'2','D2R_SKIP_MOVIES':'1','D2R_PATCH_DIR':str(patches),'DM_SHADER_ROOT':str(shader),'D2R_TEST_RECORD':str(record),'D2R_STATE_DIR':str(root/'diagnostics')}
                result=subprocess.run(['/bin/sh',str(runner),str(game),'graphics'],env=env,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(record.read_text(),'unset|unset|unset' if diagnostics=='0' else '1|1|2')
                self.assertEqual((root/'diagnostics').exists(),diagnostics=='1')
if __name__=='__main__':unittest.main()
