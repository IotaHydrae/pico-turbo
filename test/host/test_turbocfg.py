#!/usr/bin/env python3
"""Host tests for tools/turbocfg.py, and for the docs it is checked against.

Offline and deterministic: no board, no SDK.  The tool produces facts (what a
board file says, what CMake would derive, what a tier header contains); these
tests interpret them against a rule that exists independently of the tool run.

Oracles in use:

  SPEC        - the board file / C source / tune.c output format is the spec
  RELATIONSHIP- emit-then-parse is the identity; the tool's table equals pico_turbo.c's
  REQUIREMENT - the workspace CLI/exit-code convention in ../AGENTS.md

Run as `python3 test_turbocfg.py` from this directory, or via `make -C test/host`.
"""

import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOL = os.path.join(ROOT, "tools", "turbocfg.py")
BOARDS = os.path.join(ROOT, "boards")
PICO_TURBO_C = os.path.join(ROOT, "pico_turbo.c")

failures = 0
checks = 0


def test_begin(name, what, oracle_type, source, expected):
    print("[TEST] %s" % name)
    print("  %s" % what)
    print("  [ORACLE] %s" % oracle_type)
    print("  [SOURCE] %s" % source)
    print("  [EXPECTED] %s" % expected)


def check(ok, what):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("  FAIL %s" % what)
    return ok


def check_eq(got, want, what):
    return check(got == want, "%s: got %r, want %r" % (what, got, want))


def run(*args, stdin=None):
    proc = subprocess.run(
        [sys.executable, TOOL] + list(args),
        input=stdin, capture_output=True, text=True, timeout=30,
    )
    return proc.returncode, proc.stdout, proc.stderr


def run_json(*args):
    code, out, err = run(*args)
    if code != 0:
        raise AssertionError("turbocfg %s exited %d: %s" % (" ".join(args), code, err))
    return json.loads(out)


# ---------------------------------------------------------------------------


def test_board_show_matches_the_documented_files():
    test_begin(
        "test_board_show_matches_the_documented_files",
        "board-show reads back what docs/boards.md says each file carries",
        "SPEC",
        "docs/boards.md (checked against boards/*.cmake)",
        "pico 420000/133000 default even; pico_w 440000/110000 even, default extreme; "
        "pico2 564000/57000 odd-ok; weact 520000/52000 odd-ok",
    )
    # EXPECTED values are the table in docs/boards.md, not the tool's output.
    expected = {
        "pico.cmake": {
            "platform_max_khz": 420000, "flash_max_khz": None,
            "flash_requires_even": True,
            "profiles": {"safe": 240000, "turbo": 360000, "extreme": 400000},
            "default_profile": None,
        },
        "pico_w.cmake": {
            "platform_max_khz": 440000, "flash_max_khz": 110000,
            "flash_requires_even": True,
            "profiles": {"safe": 240000, "fast": 300000, "turbo": 360000,
                         "extreme": 440000},
            "default_profile": "extreme",
        },
        "pico2.cmake": {
            "platform_max_khz": 564000, "flash_max_khz": 57000,
            "flash_requires_even": False,
            "profiles": {"safe": 300000, "fast": 400000, "turbo": 520000,
                         "extreme": 564000},
            "default_profile": None,
        },
        "weact_rp2350a.cmake": {
            "platform_max_khz": 520000, "flash_max_khz": 52000,
            "flash_requires_even": False,
            "profiles": {"safe": 300000, "fast": 400000, "turbo": 500000,
                         "extreme": 520000},
            "default_profile": None,
        },
    }
    for name, want in expected.items():
        got = run_json("board-show", os.path.join(BOARDS, name), "--json")
        check_eq(got["platform_max_khz"], want["platform_max_khz"], "%s platform max" % name)
        check_eq(got["flash_max_khz"], want["flash_max_khz"], "%s flash max" % name)
        check_eq(got["flash_requires_even"], want["flash_requires_even"],
                 "%s even rule" % name)
        check_eq(got["default_profile"], want["default_profile"],
                 "%s default profile" % name)
        check_eq(sorted(got["profiles"]), sorted(want["profiles"]),
                 "%s profile names" % name)
        for profile, khz in want["profiles"].items():
            check_eq(got["profiles"][profile]["khz"], khz,
                     "%s/%s frequency" % (name, profile))


def explain(*args):
    return run_json("config-explain", *args, "--json")


def test_build_arithmetic_matches_the_documented_dividers():
    test_begin(
        "test_build_arithmetic_matches_the_documented_dividers",
        "config-explain reproduces the divider each profile was measured at",
        "SPEC",
        "docs/flash-divider.md, docs/boards.md and the per-profile comments",
        "pico_w DIV4 at 240/300/360/420/440; pico DIV2/4/4; pico2 DIV6/8/10/10; "
        "weact DIV10; no-board rp2350 DIV4",
    )
    pico_w = os.path.join(BOARDS, "pico_w.cmake")
    for khz, div, flash in ((240000, 4, 60000), (300000, 4, 75000),
                            (360000, 4, 90000), (420000, 4, 105000),
                            (440000, 4, 110000)):
        got = explain("--platform", "rp2040", "--khz", str(khz),
                      "--board-file", pico_w)
        check_eq(got["resolved_flash_div"], div, "pico_w %d div" % khz)
        check_eq(got["resolved_flash_clk_khz"], flash, "pico_w %d flash" % khz)

    pico = os.path.join(BOARDS, "pico.cmake")
    for khz, div, flash in ((240000, 2, 120000), (360000, 4, 90000),
                            (400000, 4, 100000)):
        got = explain("--platform", "rp2040", "--khz", str(khz),
                      "--board-file", pico)
        check_eq(got["resolved_flash_div"], div, "pico %d div" % khz)
        check_eq(got["resolved_flash_clk_khz"], flash, "pico %d flash" % khz)

    pico2 = os.path.join(BOARDS, "pico2.cmake")
    for khz, div, flash in ((300000, 6, 50000), (400000, 8, 50000),
                            (520000, 10, 52000), (564000, 10, 56400)):
        got = explain("--platform", "rp2350", "--khz", str(khz),
                      "--board-file", pico2)
        check_eq(got["resolved_flash_div"], div, "pico2 %d div" % khz)
        check_eq(got["resolved_flash_clk_khz"], flash, "pico2 %d flash" % khz)

    weact = os.path.join(BOARDS, "weact_rp2350a.cmake")
    got = explain("--platform", "rp2350", "--khz", "520000",
                  "--board-file", weact)
    check_eq(got["resolved_flash_div"], 10, "weact 520000 div")
    check_eq(got["resolved_flash_clk_khz"], 52000, "weact 520000 flash")

    # No board file: the CMake platform fallback, 133 MHz interface ceiling.
    got = explain("--platform", "rp2350", "--khz", "520000")
    check_eq(got["resolved_flash_div"], 4, "no-board rp2350 div")
    check_eq(got["resolved_flash_clk_khz"], 130000, "no-board rp2350 flash")


def test_voltage_table_matches_the_c_source():
    test_begin(
        "test_voltage_table_matches_the_c_source",
        "config-table equals the table compiled into pico_turbo.c",
        "RELATIONSHIP",
        "pico_turbo.c turbo_vreg_table[] (both platforms)",
        "the same (max_mhz, VREG_VOLTAGE_*) sequence, UINT32_MAX as the last row",
    )
    with open(PICO_TURBO_C, "r", encoding="utf-8") as handle:
        source = handle.read()

    rp2040_block = source.split("#if defined(PICO_RP2040)", 1)[1]
    rp2040_block = rp2040_block.split("#elif defined(PICO_RP2350)", 1)[0]
    rp2350_block = source.split("#elif defined(PICO_RP2350)", 1)[1]

    entry = re.compile(r"\{\s*(\d+|UINT32_MAX)\s*,\s*(VREG_VOLTAGE_[A-Z0-9_]+)\s*\}")

    def table_of(block):
        return [(None if m == "UINT32_MAX" else int(m), name)
                for m, name in entry.findall(block)]

    for platform, block in (("rp2040", rp2040_block), ("rp2350", rp2350_block)):
        source_table = table_of(block)
        tool_table = [(row["max_mhz"], row["vreg_voltage"])
                      for row in run_json("config-table", "--platform", platform,
                                          "--json")]
        check_eq(tool_table, source_table, "%s voltage table" % platform)


def test_table_emit_round_trips_through_table_show():
    test_begin(
        "test_table_emit_round_trips_through_table_show",
        "a generated header parses back to the tiers it was emitted from",
        "RELATIONSHIP",
        "examples/tune/tune.c header format",
        "same khz/sel sequence, COUNT = number of tiers, SAFE = 0, MAX = COUNT-1",
    )
    tiers = "235000:11,360000:13,390000:14,420000:15"
    code, out, err = run("table-emit", "--tiers", tiers,
                         "--board-id", "<board-id>", "--measured", "unspecified")
    check_eq(code, 0, "table-emit exits 0")
    check("PICO_TURBO_TUNED_COUNT 4u" in out, "emitted header declares COUNT 4")
    check("PICO_TURBO_TUNED_MAX   3u" in out, "emitted header declares MAX 3")

    code, out2, err = run("table-show", "-", stdin=out)
    check_eq(code, 0, "table-show accepts the emitted header on stdin")
    table = json.loads(run("table-show", "-", "--json", stdin=out)[1])
    check_eq([t["khz"] for t in table["tiers"]],
             [235000, 360000, 390000, 420000], "round-tripped frequencies")
    check_eq([t["vreg_sel"] for t in table["tiers"]], [11, 13, 14, 15],
             "round-tripped selectors")
    check_eq(table["count"], 4, "round-tripped count")

    # A header whose count disagrees with its entries is not a tier table.
    broken = out.replace("PICO_TURBO_TUNED_COUNT 4u", "PICO_TURBO_TUNED_COUNT 5u")
    code, _, _ = run("table-show", "-", stdin=broken)
    check_eq(code, 1, "count/entry mismatch is a FAIL, not a pass")


def test_board_propose_refuses_to_invent_a_measurement():
    test_begin(
        "test_board_propose_refuses_to_invent_a_measurement",
        "a points file with no accepted point yields INCONCLUSIVE, not a board file",
        "SPEC",
        "tools/turbocfg.py exit-code convention (../AGENTS.md)",
        "exit 5 when nothing was accepted; exit 0 with all four profiles when "
        "four points were",
    )
    with tempfile.TemporaryDirectory() as tmp:
        empty = os.path.join(tmp, "empty.json")
        with open(empty, "w", encoding="utf-8") as handle:
            json.dump({"points": [{"khz": 546000, "result": "hard-fault"}]}, handle)
        code, out, err = run("board-propose", "--board", "demo", "--platform",
                             "rp2350", "--points", empty)
        check_eq(code, 5, "no accepted point -> INCONCLUSIVE")
        check(out.strip() == "", "nothing is written to stdout on INCONCLUSIVE")

        points = os.path.join(tmp, "points.json")
        with open(points, "w", encoding="utf-8") as handle:
            json.dump({"flash_max_khz": 52000, "points": [
                {"khz": 300000, "vreg_voltage": "VREG_VOLTAGE_1_20",
                 "result": "ok", "flash_khz": 50000},
                {"khz": 400000, "vreg_voltage": "VREG_VOLTAGE_1_40",
                 "result": "ok", "flash_khz": 50000},
                {"khz": 500000, "vreg_voltage": "VREG_VOLTAGE_1_50",
                 "result": "ok", "flash_khz": 50000},
                {"khz": 520000, "vreg_voltage": "VREG_VOLTAGE_1_60",
                 "result": "ok", "flash_khz": 52000},
            ]}, handle)
        code, out, err = run("board-propose", "--board", "demo", "--platform",
                             "rp2350", "--points", points)
        check_eq(code, 0, "four accepted points -> 0")
        check("set(_PLATFORM_MAX_KHZ 520000)" in out, "top clock in the proposal")
        check("set(_FLASH_MAX_KHZ 52000)" in out, "flash ceiling in the proposal")
        check("set(_FLASH_REQUIRES_EVEN OFF)" in out, "parity rule in the proposal")
        for name in ("safe", "fast", "turbo", "extreme"):
            check('PICO_TURBO_PROFILE STREQUAL "%s"' % name in out,
                  "profile %s in the proposal" % name)

        # And the proposal must be readable back by board-show.
        path = os.path.join(tmp, "demo.cmake")
        code, _, err = run("board-propose", "--board", "demo", "--platform",
                           "rp2350", "--points", points, "--out", path)
        check_eq(code, 0, "proposal written with --out")
        parsed = run_json("board-show", path, "--json")
        check_eq(parsed["platform_max_khz"], 520000, "proposal parses back")


def test_exit_codes_follow_the_workspace_convention():
    test_begin(
        "test_exit_codes_follow_the_workspace_convention",
        "usage, environment and inconclusive outcomes are distinguishable",
        "REQUIREMENT",
        "../AGENTS.md: 0 OK, 1 FAIL, 2 INVALID_USAGE, 3 ENVIRONMENT_ERROR, 5 INCONCLUSIVE",
        "2 for an unknown subcommand, 3 for a missing file, 1 for a bad manual DIV, "
        "0 for a normal read",
    )
    code, _, _ = run("bogus-command")
    check_eq(code, 2, "unknown subcommand -> INVALID_USAGE")

    code, _, _ = run("board-show", os.path.join(BOARDS, "does-not-exist.cmake"))
    check_eq(code, 3, "missing board file -> ENVIRONMENT_ERROR")

    code, _, _ = run("config-explain", "--platform", "rp2040", "--khz", "240000",
                     "--board-file", os.path.join(BOARDS, "pico_w.cmake"),
                     "--flash-div", "5")
    check_eq(code, 1, "odd DIV on an even-only board -> FAIL")

    code, _, _ = run("table-emit", "--tiers", "not-a-tier")
    check_eq(code, 2, "malformed tier spec -> INVALID_USAGE")

    code, _, _ = run("config-explain", "--platform", "rp2040", "--khz", "500000")
    check_eq(code, 1, "clock above the platform ceiling -> FAIL")


def main():
    print("pico-turbo turbocfg host tests\n")
    test_board_show_matches_the_documented_files()
    print()
    test_build_arithmetic_matches_the_documented_dividers()
    print()
    test_voltage_table_matches_the_c_source()
    print()
    test_table_emit_round_trips_through_table_show()
    print()
    test_board_propose_refuses_to_invent_a_measurement()
    print()
    test_exit_codes_follow_the_workspace_convention()

    print("\n[RESULT] %s -- %d checks, %d failed"
          % ("PASS" if failures == 0 else "FAIL", checks, failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
