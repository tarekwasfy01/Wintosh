"""Wintosh-original capability bounds; SPDX-License-Identifier: MIT.

Boolean matrix composition tracks contracts, not syscall numbers. Unknown
edges have lower bound 0 and upper bound 1. Only passing named CTest gates
enable native lower bounds; no WSL end-to-end evidence is currently modeled.
"""
import argparse
import csv
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
STATES = {"implemented", "partial", "unknown", "missing"}


def multiply(left, right):
    """Boolean semiring: C[i,j] = OR_k (A[i,k] AND B[k,j])."""
    if not left or not right or any(len(row) != len(right) for row in left):
        raise ValueError("incompatible matrix dimensions")
    width = len(right[0])
    if not width or any(len(row) != width for row in right):
        raise ValueError("ragged matrix")
    return [[int(any(a and right[k][j] for k, a in enumerate(row)))
             for j in range(width)] for row in left]


def diagonal(values):
    return [[int(i == j and value) for j in range(len(values))]
            for i, value in enumerate(values)]


def read_capabilities(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError("empty capability matrix")
    names = [row["capability"] for row in rows]
    if any(not name for name in names) or len(set(names)) != len(names):
        raise ValueError("capability names must be nonempty and unique")
    for row in rows:
        for stage in ("native", "darwin_linux", "wsl1", "windows"):
            if row[stage] not in STATES:
                raise ValueError(f"invalid {stage} state for {row['capability']}")
    return rows


def passing_tests(path):
    if path is None:
        return set()
    root = ET.parse(path).getroot()
    results = {}
    for case in root.iter("testcase"):
        name = case.get("name")
        # Duplicate names cannot let a pass hide a failure or skipped gate.
        passed = (case.find("failure") is None and case.find("error") is None
                  and case.find("skipped") is None
                  and case.get("status", "run") in ("run", "passed"))
        results[name] = results.get(name, True) and passed
    return {name for name, passed in results.items() if name and passed}


def evaluate(rows, passed, required):
    names = [row["capability"] for row in rows]
    unknown = set(required) - set(names)
    if unknown:
        raise ValueError(f"unknown required capabilities: {', '.join(sorted(unknown))}")
    native_lower = diagonal([row["native"] == "implemented" and
                             bool(row["native_test"]) and row["native_test"] in passed
                             for row in rows])
    native_upper = diagonal([row["native"] != "missing" for row in rows])
    # Each diagonal retains the SAME semantic contract through every layer.
    # Similar names (e.g. Linux and Darwin mmap flags) do not create an edge.
    upper_stages = [diagonal([row[stage] != "missing" for row in rows])
                    for stage in ("darwin_linux", "wsl1", "windows")]
    wsl_upper = multiply(multiply(*upper_stages[:2]), upper_stages[2])
    wsl_lower = diagonal([False] * len(rows))
    routes = {}
    for route, lower, upper in (("native", native_lower, native_upper),
                                ("darwin_linux_wsl1_windows", wsl_lower, wsl_upper)):
        checks = []
        for name in required:
            i = names.index(name)
            checks.append({"capability": name, "lower": lower[i][i],
                           "upper": upper[i][i], "scope": rows[i]["scope"]})
        routes[route] = {"lower_matrix": lower, "upper_matrix": upper,
                         "requirements": checks,
                         "all_required_demonstrated": all(c["lower"] for c in checks),
                         "missing": [c["capability"] for c in checks if not c["upper"]],
                         "unproven": [c["capability"] for c in checks
                                      if c["upper"] and not c["lower"]]}
    return {"capability_order": names, "routes": routes,
            "boundary": "Bounds apply only to named fixture/adapter contracts in the supplied test run; no application compatibility proof."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path,
                        default=ROOT / "docs/porting/TRANSFORM-CAPABILITIES.csv")
    parser.add_argument("--junit", type=Path, help="CTest --output-junit result file")
    parser.add_argument("--require", action="append", default=[])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", choices=("native", "darwin_linux_wsl1_windows"),
                        help="exit 1 unless every requested contract is demonstrated on this route")
    args = parser.parse_args()
    try:
        rows = read_capabilities(args.matrix)
        report = evaluate(rows, passing_tests(args.junit),
                          args.require or [row["capability"] for row in rows])
        report["evidence_file"] = str(args.junit.resolve()) if args.junit else None
        rendered = json.dumps(report, indent=2) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        print(rendered, end="")
        if args.check and not report["routes"][args.check]["all_required_demonstrated"]:
            raise SystemExit(1)
    except (ValueError, KeyError, OSError, ET.ParseError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
