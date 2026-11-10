"""Run public readers and real inspector pages against controlled receipts."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class DataRefactor(unittest.TestCase):
    def run_fixture(self, source, cxx=False, sanitize=False, includes=(), failure=False, tsan=False):
        compiler = shutil.which('clang++' if cxx else 'clang')
        if not compiler:
            self.skipTest('Clang unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'test'
            flags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if sanitize else []
            if tsan:
                flags = ['-fsanitize=thread', '-fno-omit-frame-pointer']
            args = [compiler, '-x', 'c++' if cxx else 'c', '-std=c++14' if cxx else '-std=c11',
                    '-Wall', '-Wextra', '-Werror', '-O1', '-g', *flags,
                    *['-I'+str(path) for path in includes],
                    '-I'+str(ROOT/'include'), '-I'+str(ROOT/'examples/library-inspector'), '-pthread']
            result = subprocess.run([*args, str(ROOT/'tests/fixtures'/source), '-lm', '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            if failure:
                self.assertNotEqual(result.returncode, 0, result.stdout+result.stderr)
                self.assertIn('assert', result.stderr.lower())
            else:
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_readers_c11(self):
        self.run_fixture('data-helpers.c')

    def test_readers_cpp14(self):
        self.run_fixture('data-helpers.c', cxx=True)

    def test_readers_asan_ubsan(self):
        self.run_fixture('data-helpers.c', sanitize=True)

    def test_deck_status_readers(self):
        self.run_fixture('deck-status-helpers.c')
        self.run_fixture('deck-status-helpers.c', cxx=True)
        self.run_fixture('deck-status-helpers.c', sanitize=True)

    def test_authoritative_core_clock(self):
        checkout = pathlib.Path(os.environ.get('AZ_CORE_TEST_ROOT', ROOT.parent/'xdj-az-mods'))
        core = checkout/'src/common'
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path/'azmod-timestamps.h').write_text((core/'aztimestamp.h').read_text())
            (path/'azmod-data-helpers.h').write_text((ROOT/'include/azmod-data-helpers.h').read_text())
            self.run_fixture('data-helpers.c', includes=[path])
            self.assertEqual((core/'aztimestamp.h').read_bytes(), (ROOT/'include/azmod-timestamps.h').read_bytes())

    def test_live_transitions(self):
        self.run_fixture('inspector-live.c', sanitize=True)

    def test_control_callbacks(self):
        self.run_fixture('inspector-control.c', sanitize=True)

    def test_cue_rendering(self):
        self.run_fixture('inspector-cues.c', sanitize=True)

    def test_cue_command_client(self):
        self.run_fixture('cue-command-async.c', sanitize=True)

    def test_regression_controls(self):
        variants = [
            ('live.c', 'inspector-live.c',
             'memset(out, 0, sizeof lines[e->deck]);',
             '(void)out;'),
            ('cues.c', 'inspector-cues.c',
             'if (c->valid & AZ_CUE_START)', 'if (1)'),
            ('control.c', 'inspector-control.c',
             'command.phase = WAIT_TERMINAL;',
             'command.phase = result.outcome == AZ_COMMAND_ACCEPTED ? WAIT_TERMINAL : IDLE;'),
            ('control.c', 'inspector-control.c',
             'status = library->command_submit_async(ID, &request, complete, &command, &result);',
             'status = library->command_submit_async(ID, &request, complete, &command, &result); '
             'if (status == AZ_INVALID) snprintf(lines[3], sizeof lines[3], "invalid outcome %u", result.outcome);'),
        ]
        for source, fixture, old, new in variants:
            with self.subTest(source=source, mutation=old), tempfile.TemporaryDirectory() as directory:
                path = pathlib.Path(directory)
                original = (ROOT/'examples/library-inspector'/source).read_text()
                self.assertEqual(original.count(old), 1)
                (path/source).write_text(original.replace(old, new))
                self.run_fixture(fixture, sanitize=True, includes=[path], failure=True)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            original = (ROOT/'include/azmod-timestamps.h').read_text()
            old = 'if (first.negative || second.negative) return AZ_TIME_INVALID;'
            self.assertEqual(original.count(old), 1)
            (path/'azmod-timestamps.h').write_text(original.replace(old, '(void)0;'))
            (path/'azmod-data-helpers.h').write_text((ROOT/'include/azmod-data-helpers.h').read_text())
            self.run_fixture('data-helpers.c', includes=[path], failure=True)

    def test_log_transport_asan_ubsan(self):
        self.run_fixture('command-probe-queue.c', sanitize=True)

    def test_log_transport_tsan(self):
        self.run_fixture('command-probe-queue.c', tsan=True)
