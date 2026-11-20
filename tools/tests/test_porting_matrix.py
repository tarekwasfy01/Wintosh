"""Regression gates for conservative compatibility composition."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "porting_matrix", Path(__file__).resolve().parents[1] / "porting_matrix.py")
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)


def capability(name="fixture", native="implemented", wsl="unknown"):
    return dict(capability=name, native=native, native_test="gate",
                darwin_linux="unknown", wsl1=wsl, windows="unknown", scope="fixture only")


class MatrixTests(unittest.TestCase):
    def test_composition_requires_matching_intermediate_contract(self):
        self.assertEqual(matrix.multiply([[1, 0]], [[0], [1]]), [[0]])
        self.assertEqual(matrix.multiply([[0, 1]], [[0], [1]]), [[1]])

    def test_unknown_is_not_proof(self):
        report = matrix.evaluate([capability()], set(), ["fixture"])
        for route in report["routes"].values():
            self.assertFalse(route["all_required_demonstrated"])
            self.assertEqual(route["unproven"], ["fixture"])

    def test_native_pass_does_not_prove_wsl(self):
        report = matrix.evaluate([capability()], {"gate"}, ["fixture"])
        self.assertTrue(report["routes"]["native"]["all_required_demonstrated"])
        self.assertFalse(report["routes"]["darwin_linux_wsl1_windows"]["all_required_demonstrated"])

    def test_missing_stage_breaks_chain(self):
        report = matrix.evaluate([capability(wsl="missing")], {"gate"}, ["fixture"])
        self.assertEqual(report["routes"]["darwin_linux_wsl1_windows"]["missing"], ["fixture"])

    def test_every_required_capability_must_pass(self):
        rows = [capability(), capability("unwind", native="missing")]
        self.assertFalse(matrix.evaluate(rows, {"gate"}, ["fixture", "unwind"])
                         ["routes"]["native"]["all_required_demonstrated"])

    def test_unknown_requirement_is_error(self):
        with self.assertRaises(ValueError):
            matrix.evaluate([capability()], {"gate"}, ["typo"])

    def test_junit_failures_skips_errors_and_duplicates(self):
        xml = '''<testsuites><testsuite>
        <testcase name="pass" status="run"/>
        <testcase name="fail"><failure/></testcase>
        <testcase name="skip"><skipped/></testcase>
        <testcase name="error"><error/></testcase>
        <testcase name="duplicate"/><testcase name="duplicate"><failure/></testcase>
        <testcase name="disabled" status="notrun"/>
        </testsuite></testsuites>'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "results.xml"
            path.write_text(xml, encoding="utf-8")
            self.assertEqual(matrix.passing_tests(path), {"pass"})


if __name__ == "__main__":
    unittest.main()
