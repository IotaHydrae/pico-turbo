#!/usr/bin/env python3
"""turbocfg - the pico-turbo board, tier-table and build-arithmetic utility.

Produces facts, not conclusions: it parses what a board file or a generated tier
header says, reproduces the CMake flash-divider/voltage arithmetic, and turns a set
of measured points into a board-file proposal.  Tests decide what an acceptable
board file looks like; this tool only reports and proposes.

Offline and deterministic.  No board, no SDK.

Usage:
    turbocfg board-show FILE [--json]
    turbocfg board-propose --board NAME --platform rp2040|rp2350 --points FILE
                           [--flash-max-khz N] [--json] [--out FILE]
    turbocfg table-show FILE|-
    turbocfg table-emit --tiers KHZ:SEL,... [--board-id ID] [--measured TEXT]
    turbocfg config-explain --platform rp2040|rp2350 --khz KHZ
                           [--board-file FILE] [--max-clk-khz N]
                           [--flash-max-khz N] [--flash-div N]
                           [--vreg NAME] [--autotune] [--json]
    turbocfg config-table --platform rp2040|rp2350 [--json]

Common options: --help, --version, --json, --quiet, --verbose, --timeout SECONDS.

Exit codes: 0 OK, 1 FAIL, 2 INVALID_USAGE, 3 ENVIRONMENT_ERROR, 4 TIMEOUT,
5 INCONCLUSIVE.
"""

import argparse
import json
import os
import re
import signal
import sys

VERSION = "0.1.0"

EXIT_OK = 0
EXIT_FAIL = 1
EXIT_USAGE = 2
EXIT_ENV = 3
EXIT_TIMEOUT = 4
EXIT_INCONCLUSIVE = 5

# The regulator register encodings this project's records name.  These are register
# values, not millivolts; the authoritative table for the library itself is parsed
# out of pico_turbo.c by test/host/test_turbocfg.py.
VREG_NAME_BY_SEL = {
    11: "VREG_VOLTAGE_1_10",
    13: "VREG_VOLTAGE_1_20",
    14: "VREG_VOLTAGE_1_25",
    15: "VREG_VOLTAGE_1_30",
    16: "VREG_VOLTAGE_1_35",
    17: "VREG_VOLTAGE_1_40",
    18: "VREG_VOLTAGE_1_50",
    19: "VREG_VOLTAGE_1_60",
    20: "VREG_VOLTAGE_1_65",
    21: "VREG_VOLTAGE_1_70",
}
VREG_DEFAULT_SEL = 11  # stock: 1.10 V on both platforms

# The frequency -> lowest-voltage table from pico_turbo.c.  Kept here so the tool can
# answer "what would this build ask for" without a compiler; the drift guard in the
# host tests compares it against the C source itself.
VREG_TABLE = {
    "rp2040": [
        (266, "VREG_VOLTAGE_DEFAULT"),
        (360, "VREG_VOLTAGE_1_20"),
        (396, "VREG_VOLTAGE_1_25"),
        (None, "VREG_VOLTAGE_1_30"),
    ],
    "rp2350": [
        (150, "VREG_VOLTAGE_DEFAULT"),
        (300, "VREG_VOLTAGE_1_20"),
        (384, "VREG_VOLTAGE_1_30"),
        (440, "VREG_VOLTAGE_1_40"),
        (500, "VREG_VOLTAGE_1_50"),
        (None, "VREG_VOLTAGE_1_60"),
    ],
}

# CMake fallbacks from CMakeLists.txt when no board file matches.
PLATFORM_FALLBACK = {
    "rp2040": {"max_khz": 420000, "boot2_div": 2, "requires_even": True},
    "rp2350": {"max_khz": 520000, "boot2_div": 4, "requires_even": False},
}

DEFAULT_FLASH_MAX_KHZ = 133000


class ToolError(Exception):
    def __init__(self, message, code=EXIT_FAIL):
        super().__init__(message)
        self.code = code


def _timeout_handler(signum, frame):
    raise ToolError("timed out", EXIT_TIMEOUT)


def add_common(parser, suppress):
    """Options accepted both before and after the subcommand.

    With suppress=True they appear in the namespace only when actually given, so a
    subparser does not overwrite a value set before the subcommand.
    """
    def kw():
        return {"default": argparse.SUPPRESS} if suppress else {}

    parser.add_argument("--json", action="store_true", help="machine-readable output",
                        **kw())
    parser.add_argument("--quiet", action="store_true", help="suppress progress text",
                        **kw())
    parser.add_argument("--verbose", action="store_true", help="explain the arithmetic",
                        **kw())
    parser.add_argument("--timeout", type=float, **kw(),
                        help="seconds before the tool gives up (exit 4); offline "
                             "commands finish immediately")


def parse_common():
    parser = argparse.ArgumentParser(
        prog="turbocfg",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--version", action="version", version="turbocfg " + VERSION)
    add_common(parser, suppress=False)
    return parser


def emit(args, text):
    if not args.quiet:
        sys.stdout.write(text if text.endswith("\n") else text + "\n")


# ---------------------------------------------------------------------------
# A CMake-subset parser: enough for boards/*.cmake, deliberately not a CMake.
# ---------------------------------------------------------------------------

_SET_RE = re.compile(r"^set\(\s*([A-Za-z0-9_]+)\s+(.*?)\s*\)$")
_PROFILE_RE = re.compile(
    r'^(?:if|elseif)\(\s*PICO_TURBO_PROFILE\s+STREQUAL\s+"([^"]*)"\s*\)$'
)


def parse_board_file(path):
    if not os.path.isfile(path):
        raise ToolError("no such board file: %s" % path, EXIT_ENV)

    board = {
        "file": path,
        "platform_max_khz": None,
        "flash_max_khz": None,
        "flash_requires_even": None,
        "boot2_default_div": None,
        "default_profile": None,
        "has_none_profile": False,
        "profiles": {},
    }
    profile = None
    in_chain = False

    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue

            match = _PROFILE_RE.match(line)
            if match:
                name = match.group(1)
                if name in ("", "none"):
                    # "none" is the opt-out to stock clocks, not a profile.
                    board["has_none_profile"] = board["has_none_profile"] or (
                        name == "none"
                    )
                    profile = None
                else:
                    profile = name
                    board["profiles"].setdefault(name, {})
                in_chain = True
                continue

            if line.startswith("endif()"):
                if in_chain:
                    profile = None
                    in_chain = False
                continue

            match = _SET_RE.match(line)
            if not match:
                continue
            name, value = match.group(1), match.group(2).strip('"')

            if profile is not None:
                key = {
                    "PICO_TURBO_SYS_CLK_KHZ": "khz",
                    "PICO_TURBO_VREG_VOLTAGE": "vreg_voltage",
                    "PICO_TURBO_FLASH_CLK_DIV": "flash_div",
                }.get(name)
                if key:
                    board["profiles"][profile][key] = _as_number(value)
                continue

            if name == "PICO_TURBO_PROFILE" and value:
                board["default_profile"] = value
            elif name == "_PLATFORM_MAX_KHZ":
                board["platform_max_khz"] = _as_number(value)
            elif name == "_FLASH_MAX_KHZ":
                board["flash_max_khz"] = _as_number(value)
            elif name == "_FLASH_REQUIRES_EVEN":
                board["flash_requires_even"] = value.upper() == "ON"
            elif name == "_BOOT2_DEFAULT_DIV":
                board["boot2_default_div"] = _as_number(value)

    return board


def _as_number(value):
    try:
        return int(value, 0)
    except ValueError:
        return value


# ---------------------------------------------------------------------------
# Board file emission
# ---------------------------------------------------------------------------

PROFILE_NAMES = ["safe", "fast", "turbo", "extreme"]


def propose_board(board_name, platform, points, flash_max_khz, requires_even,
                  boot2_div):
    good = [p for p in points if str(p.get("result", "")).lower() in ("ok", "stable")]
    good = sorted(good, key=lambda p: int(p["khz"]))
    if not good:
        raise ToolError("no accepted points in the input -- nothing to propose",
                        EXIT_INCONCLUSIVE)

    # Deduplicate by frequency, keep the last (highest voltage if a step was retried).
    by_khz = {}
    for point in good:
        by_khz[int(point["khz"])] = point
    ladder = [by_khz[k] for k in sorted(by_khz)]

    picks = []
    for frac in (0.0, 0.34, 0.67, 1.0):
        picks.append(ladder[min(len(ladder) - 1, int(round(frac * (len(ladder) - 1))))])
    seen, unique = set(), []
    for point in picks:
        if point["khz"] not in seen:
            seen.add(point["khz"])
            unique.append(point)

    if flash_max_khz is None:
        flash_max_khz = max(
            (int(p["flash_khz"]) for p in good if p.get("flash_khz")), default=None
        )

    lines = []
    lines.append("# Board config: %s" % board_name)
    lines.append("#")
    lines.append("# Generated by tools/turbocfg.py from measured points.  Review it before")
    lines.append("# use: a number here is only as good as the run it came from, and the")
    lines.append("# same PICO_BOARD name can cover a different flash part.")
    lines.append("")
    lines.append("# The highest clock these points validated.")
    lines.append("set(_PLATFORM_MAX_KHZ %d)" % ladder[-1]["khz"])
    if flash_max_khz:
        lines.append("")
        lines.append("# The fastest flash clock these points ran at.  The QSPI interface")
        lines.append("# allows 133 MHz; this is what the board was shown to hold.")
        lines.append("set(_FLASH_MAX_KHZ %d)" % int(flash_max_khz))
    lines.append("set(_BOOT2_DEFAULT_DIV %d)" % boot2_div)
    lines.append("set(_FLASH_REQUIRES_EVEN %s)" % ("ON" if requires_even else "OFF"))
    lines.append("")

    names = PROFILE_NAMES[: len(unique)]
    for index, (name, point) in enumerate(zip(names, unique)):
        keyword = "if" if index == 0 else "elseif"
        lines.append('%s(PICO_TURBO_PROFILE STREQUAL "%s")' % (keyword, name))
        lines.append("    set(PICO_TURBO_SYS_CLK_KHZ %d)" % int(point["khz"]))
        vreg = point.get("vreg_voltage")
        if vreg and vreg != "VREG_VOLTAGE_DEFAULT":
            lines.append("    set(PICO_TURBO_VREG_VOLTAGE %s)" % vreg)
        else:
            lines.append("    # stock voltage")
        div = point.get("flash_div") or _derive_div(
            int(point["khz"]), flash_max_khz, requires_even
        )
        if div:
            lines.append("    set(PICO_TURBO_FLASH_CLK_DIV %d)" % div)
    lines.append('elseif(NOT PICO_TURBO_PROFILE STREQUAL "")')
    lines.append('    message(FATAL_ERROR "pico-turbo: unknown profile '
                 "'${PICO_TURBO_PROFILE}' for board %s\")" % board_name)
    lines.append("endif()")
    lines.append("")

    return "\n".join(lines)


def _derive_div(top_khz, flash_max_khz, requires_even):
    if not flash_max_khz:
        return None
    div = max(2, -(-int(top_khz) // int(flash_max_khz)))
    if requires_even and div % 2:
        div += 1
    return div


# ---------------------------------------------------------------------------
# Tier table
# ---------------------------------------------------------------------------

_ENTRY_RE = re.compile(r"\{\s*(\d+)u?\s*,\s*(\d+)u?\s*\}")
_DEFINE_RE = re.compile(r"#define\s+PICO_TURBO_TUNED_([A-Z_]+)\s+\"?([^\"\s]+)\"?")


def parse_tier_header(text):
    table = {"board_id": None, "count": None, "safe": None, "max": None, "tiers": []}
    for name, value in _DEFINE_RE.findall(text):
        if name == "BOARD_ID":
            table["board_id"] = value
        elif name == "COUNT":
            table["count"] = int(value.rstrip("u"))
        elif name == "SAFE":
            table["safe"] = int(value.rstrip("u"))
        elif name == "MAX":
            table["max"] = int(value.rstrip("u"))
    for khz, sel in _ENTRY_RE.findall(text):
        table["tiers"].append({
            "khz": int(khz),
            "vreg_sel": int(sel),
            "vreg_voltage": VREG_NAME_BY_SEL.get(int(sel)),
        })
    if table["count"] is None:
        raise ToolError("not a pico_turbo_config.h: no PICO_TURBO_TUNED_COUNT")
    if table["count"] != len(table["tiers"]):
        raise ToolError("PICO_TURBO_TUNED_COUNT is %d but there are %d entries"
                        % (table["count"], len(table["tiers"])))
    return table


def emit_tier_header(tiers, board_id, measured):
    if not tiers:
        raise ToolError("no tiers given", EXIT_USAGE)
    lines = []
    lines.append("/*")
    lines.append(" * pico_turbo: stable configurations, emitted by tools/turbocfg.py.")
    lines.append(" *")
    lines.append(" * A transformation of the tiers it was given, not a measurement: the")
    lines.append(" * frequencies and voltages are only as good as their source run.")
    lines.append(" *")
    lines.append(" * board id : %s" % board_id)
    lines.append(" * measured : %s" % measured)
    lines.append(" * tiers    : %d" % len(tiers))
    lines.append(" */")
    lines.append("#ifndef PICO_TURBO_CONFIG_H")
    lines.append("#define PICO_TURBO_CONFIG_H")
    lines.append("")
    lines.append('#include "pico_turbo.h"')
    lines.append("")
    lines.append('#define PICO_TURBO_TUNED_BOARD_ID "%s"' % board_id)
    lines.append("#define PICO_TURBO_TUNED_COUNT %du" % len(tiers))
    lines.append("#define PICO_TURBO_TUNED_SAFE  0u  /* slowest, lowest voltage */")
    lines.append("#define PICO_TURBO_TUNED_MAX   %du  /* fastest in this table */"
                 % (len(tiers) - 1))
    lines.append("")
    lines.append("/* khz = clk_sys, vreg_sel = VREG_VOLTAGE_* register encoding */")
    lines.append("static const pico_turbo_config_t "
                 "pico_turbo_tuned_configs[PICO_TURBO_TUNED_COUNT] = {")
    for tier in tiers:
        lines.append("\t{ %du, %du }, /* %u MHz @ sel %u */"
                     % (tier["khz"], tier["vreg_sel"], tier["khz"] // 1000,
                        tier["vreg_sel"]))
    lines.append("};")
    lines.append("")
    lines.append("#endif /* PICO_TURBO_CONFIG_H */")
    lines.append("")
    return "\n".join(lines)


def parse_tier_spec(spec):
    tiers = []
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        if ":" not in item:
            raise ToolError("tier %r is not KHZ:SEL" % item, EXIT_USAGE)
        khz, sel = item.split(":", 1)
        try:
            khz, sel = int(khz), int(sel)
        except ValueError:
            raise ToolError("tier %r has a non-numeric field" % item, EXIT_USAGE)
        if khz <= 0 or not 0 <= sel <= 0xFF:
            raise ToolError("tier %r is out of range" % item, EXIT_USAGE)
        tiers.append({"khz": khz, "vreg_sel": sel})
    if not tiers:
        raise ToolError("--tiers is empty", EXIT_USAGE)
    return tiers


# ---------------------------------------------------------------------------
# Build arithmetic (mirrors CMakeLists.txt)
# ---------------------------------------------------------------------------

def voltage_for(platform, khz):
    mhz = khz // 1000
    for max_mhz, name in VREG_TABLE[platform]:
        if max_mhz is None or mhz <= max_mhz:
            return name
    return VREG_TABLE[platform][-1][1]


def explain(platform, khz, board, max_clk_khz, flash_max_khz, flash_div, vreg,
            autotune):
    fallback = PLATFORM_FALLBACK[platform]
    platform_max = (board or {}).get("platform_max_khz") or fallback["max_khz"]
    requires_even = (board or {}).get("flash_requires_even")
    if requires_even is None:
        requires_even = fallback["requires_even"]

    if khz > platform_max:
        raise ToolError("%d kHz exceeds the platform maximum %d kHz"
                        % (khz, platform_max))

    if max_clk_khz is None:
        max_clk_khz = platform_max if autotune else khz

    if flash_max_khz is None:
        flash_max_khz = (board or {}).get("flash_max_khz") or DEFAULT_FLASH_MAX_KHZ

    if flash_div is not None:
        if flash_div < 2:
            raise ToolError("flash DIV must be >= 2")
        if requires_even and flash_div % 2:
            raise ToolError("this board requires an even flash DIV")
        div = flash_div
    else:
        div = _derive_div(max_clk_khz, flash_max_khz, requires_even)

    result = {
        "platform": platform,
        "requested_khz": khz,
        "resolved_clk_khz": khz,
        "resolved_max_clk_khz": max_clk_khz,
        "platform_max_khz": platform_max,
        "resolved_flash_div": div,
        "resolved_flash_clk_khz": max_clk_khz // div if div else None,
        "vreg_voltage": vreg or voltage_for(platform, khz),
        "flash_max_khz": flash_max_khz,
        "flash_requires_even": requires_even,
    }
    return result


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_board_show(args):
    board = parse_board_file(args.file)
    if args.json:
        print(json.dumps(board, indent=2, sort_keys=True))
        return EXIT_OK
    if not args.quiet:
        print("board file      : %s" % board["file"])
        print("platform max    : %s" % _fmt(board["platform_max_khz"]))
        print("flash max       : %s" % _fmt(board["flash_max_khz"]))
        print("even DIV needed : %s" % _fmt(board["flash_requires_even"]))
        print("boot2 default   : %s" % _fmt(board["boot2_default_div"]))
        print("default profile : %s" % _fmt(board["default_profile"]))
        for name in sorted(board["profiles"]):
            profile = board["profiles"][name]
            print("  %-8s %s kHz, %s, DIV %s"
                  % (name, _fmt(profile.get("khz")),
                     _fmt(profile.get("vreg_voltage")),
                     _fmt(profile.get("flash_div"))))
    return EXIT_OK


def cmd_board_propose(args):
    if not os.path.isfile(args.points):
        raise ToolError("no such points file: %s" % args.points, EXIT_ENV)
    with open(args.points, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    points = data.get("points") if isinstance(data, dict) else data
    if not isinstance(points, list):
        raise ToolError("points file must hold a list or {'points': [...]}")

    platform = args.platform
    fallback = PLATFORM_FALLBACK[platform]
    requires_even = fallback["requires_even"]
    if args.requires_even is not None:
        requires_even = args.requires_even
    boot2_div = args.boot2_default_div or fallback["boot2_div"]
    if data is not None and isinstance(data, dict) and data.get("flash_max_khz"):
        if args.flash_max_khz is None:
            args.flash_max_khz = int(data["flash_max_khz"])

    text = propose_board(args.board, platform, points, args.flash_max_khz,
                         requires_even, boot2_div)

    if args.json:
        print(json.dumps({
            "board": args.board,
            "platform": platform,
            "flash_max_khz": args.flash_max_khz,
            "flash_requires_even": requires_even,
            "content": text,
        }, indent=2, sort_keys=True))
        return EXIT_OK

    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(text)
        emit(args, "wrote %s" % args.out)
    else:
        sys.stdout.write(text)
    return EXIT_OK


def cmd_table_show(args):
    if args.file == "-":
        text = sys.stdin.read()
    else:
        if not os.path.isfile(args.file):
            raise ToolError("no such header: %s" % args.file, EXIT_ENV)
        with open(args.file, "r", encoding="utf-8") as handle:
            text = handle.read()
    table = parse_tier_header(text)
    if args.json:
        print(json.dumps(table, indent=2, sort_keys=True))
        return EXIT_OK
    if not args.quiet:
        print("board id : %s" % _fmt(table["board_id"]))
        print("tiers    : %d (safe %s, max %s)"
              % (table["count"], _fmt(table["safe"]), _fmt(table["max"])))
        for index, tier in enumerate(table["tiers"]):
            print("  %2d  %6d kHz  sel %2d  %s"
                  % (index, tier["khz"], tier["vreg_sel"],
                     _fmt(tier["vreg_voltage"])))
    return EXIT_OK


def cmd_table_emit(args):
    tiers = parse_tier_spec(args.tiers)
    text = emit_tier_header(tiers, args.board_id, args.measured)
    if args.json:
        print(json.dumps({"board_id": args.board_id, "tiers": tiers,
                          "content": text}, indent=2, sort_keys=True))
        return EXIT_OK
    sys.stdout.write(text)
    return EXIT_OK


def cmd_config_explain(args):
    board = parse_board_file(args.board_file) if args.board_file else None
    result = explain(args.platform, args.khz, board, args.max_clk_khz,
                     args.flash_max_khz, args.flash_div, args.vreg, args.autotune)
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
        return EXIT_OK
    if not args.quiet:
        print("%s @ %d kHz" % (args.platform, args.khz))
        print("  voltage          : %s" % result["vreg_voltage"])
        print("  range top        : %d kHz" % result["resolved_max_clk_khz"])
        print("  flash DIV        : %d%s"
              % (result["resolved_flash_div"],
                 " (kept even)" if result["flash_requires_even"] else ""))
        print("  flash clock (top): %d kHz" % result["resolved_flash_clk_khz"])
        if args.verbose:
            print("  derived from     : ceil(%d / %d), min 2"
                  % (result["resolved_max_clk_khz"], result["flash_max_khz"]))
    return EXIT_OK


def cmd_config_table(args):
    table = [{"max_mhz": max_mhz, "vreg_voltage": name}
             for max_mhz, name in VREG_TABLE[args.platform]]
    if args.json:
        print(json.dumps(table, indent=2, sort_keys=True))
        return EXIT_OK
    for row in table:
        print("<= %-5s MHz  %s"
              % (_fmt(row["max_mhz"]), row["vreg_voltage"]))
    return EXIT_OK


def _fmt(value):
    if value is None:
        return "-"
    if isinstance(value, bool):
        return "ON" if value else "OFF"
    return value


# ---------------------------------------------------------------------------
# Argument wiring
# ---------------------------------------------------------------------------

def build_parser():
    parser = parse_common()
    sub = parser.add_subparsers(dest="command")

    def child(name, help_text):
        p = sub.add_parser(name, help=help_text, parents=[], add_help=True)
        add_common(p, suppress=True)
        return p

    p = child("board-show", "parse a boards/*.cmake file")
    p.add_argument("file")
    p.set_defaults(func=cmd_board_show)

    p = child("board-propose", "turn measured points into a board file")
    p.add_argument("--board", required=True)
    p.add_argument("--platform", choices=["rp2040", "rp2350"], required=True)
    p.add_argument("--points", required=True, help="JSON list of measured points")
    p.add_argument("--flash-max-khz", type=int, default=None)
    p.add_argument("--requires-even", dest="requires_even", action="store_true",
                   default=None)
    p.add_argument("--no-requires-even", dest="requires_even", action="store_false",
                   default=None)
    p.add_argument("--boot2-default-div", type=int, default=None)
    p.add_argument("--out", default=None)
    p.set_defaults(func=cmd_board_propose)

    p = child("table-show", "parse a generated pico_turbo_config.h")
    p.add_argument("file")
    p.set_defaults(func=cmd_table_show)

    p = child("table-emit", "emit a pico_turbo_config.h from tiers")
    p.add_argument("--tiers", required=True, help="KHZ:SEL, e.g. 235000:11,360000:13")
    p.add_argument("--board-id", default="<board-id>")
    p.add_argument("--measured", default="unspecified")
    p.set_defaults(func=cmd_table_emit)

    p = child("config-explain", "reproduce the build's arithmetic")
    p.add_argument("--platform", choices=["rp2040", "rp2350"], required=True)
    p.add_argument("--khz", type=int, required=True)
    p.add_argument("--board-file", default=None)
    p.add_argument("--max-clk-khz", type=int, default=None)
    p.add_argument("--flash-max-khz", type=int, default=None)
    p.add_argument("--flash-div", type=int, default=None)
    p.add_argument("--vreg", default=None)
    p.add_argument("--autotune", action="store_true")
    p.set_defaults(func=cmd_config_explain)

    p = child("config-table", "the frequency-to-voltage table")
    p.add_argument("--platform", choices=["rp2040", "rp2350"], required=True)
    p.set_defaults(func=cmd_config_table)

    return parser


def main(argv):
    parser = build_parser()
    args = parser.parse_args(argv)
    if not getattr(args, "command", None):
        parser.print_help()
        return EXIT_USAGE

    if args.timeout:
        signal.signal(signal.SIGALRM, _timeout_handler)
        signal.setitimer(signal.ITIMER_REAL, args.timeout)

    if args.json:
        args.quiet = True

    try:
        return args.func(args)
    except ToolError as exc:
        sys.stderr.write("turbocfg: %s\n" % exc)
        return exc.code
    except (ValueError, KeyError) as exc:
        sys.stderr.write("turbocfg: malformed input: %s\n" % exc)
        return EXIT_FAIL
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
