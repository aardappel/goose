"""Compiler discovery prefers speed, while --exe keeps assertion builds explicit."""

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

# A copy of scripts/toolchain.py of this test's own: run_tests.py runs the
# test on a thread beside jobs that read the shared module's REPO_ROOT, which
# patching it here would move under them.
_spec = importlib.util.spec_from_file_location(
    "toolchain_selection_tc", Path(__file__).resolve().parents[1] / "scripts" / "toolchain.py")
tc = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = tc
_spec.loader.exec_module(tc)


class CompilerSelection(unittest.TestCase):
    def test_preference_and_fallback(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(tc, "REPO_ROOT", Path(tmp)):
            paths = [Path(tmp) / "build" / sub / ("goose" + tc.EXE_SUFFIX)
                     for sub in ("Release", "RelWithDebInfo", "", "Debug")]
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            self.assertEqual(tc.find_goose(paths[-1]), paths[-1].resolve())
            for path in paths:
                self.assertEqual(tc.find_goose(), path)
                path.unlink()
            with self.assertRaises(SystemExit):
                tc.find_goose()
            with self.assertRaises(SystemExit):
                tc.find_goose(paths[0])


if __name__ == "__main__":
    unittest.main()
