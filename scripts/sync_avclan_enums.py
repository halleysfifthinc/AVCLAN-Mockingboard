#!/usr/bin/env python3
"""Keep the C++ Device/Action enums in sync with the Lua dissector tables.

The Wireshark dissector in scripts/packet-analysis/avclan_plugin.lua is the
authoritative source of truth for AVC-LAN device and action names. Its
`known_devices` / `known_actions` tables and the firmware's C++ enums
(`enum class Device` in src/avclan/device.hpp, `enum ... Action` in
src/avclan/avclan.h) encode the same numeric values and drift apart by hand.

This tool reconciles them, matching entries *by value* so a value renamed in
the dissector is propagated into C++ (and to every reference under src/), not
just added/removed.

    Default (no flags)   lint the dissector names AND report enum drift; write
                         nothing; exit non-zero if either has problems. This is
                         what the pre-commit hook runs.
    --fix                lint first (never fold an invalid name into C++), then
                         apply: rewrite the enum bodies and search-replace the
                         renamed members across src/.
    --lint               validate the dissector names only.
    --verbose            list every reference-site replacement made by --fix.

Reconciliation rules (see the project plan for the full rationale):

  * Match by value. Names are compared with a case/separator-insensitive
    canonical key, so a case-only difference (LIST_FUNCTIONS_REQ vs
    List_Functions_Req) is NOT a change -- the existing C++ spelling is kept.
  * A beyond-case difference is a rename: the C++ member is renamed to the
    house-style form of the dissector name and all references are updated.
  * Full mirror: every active dissector entry missing from C++ is added; if C++
    carries a value the dissector lacks, that is an error (reported, not
    dropped). Commented-out ("pencil-in") Lua entries are ignored.
  * New/renamed members follow each enum's house style: Device -> UPPER_SNAKE,
    Action -> Title_Case (with a small acronym allowlist kept uppercase).
  * New members are inserted at their position in the Lua table (adjacent to
    their nearest Lua neighbour that already exists in C++).

The dissector is expected to keep *approximately* valid identifiers: spaces and
punctuation are silently converted to single underscores. Only two hard rules
are enforced by --lint (a name must not start with a digit, and must be at
least 3 characters); the 3-char floor also keeps the reference search-replace
from colliding on 2-character tokens.

Runs as a pre-commit hook via .githooks/pre-commit; that directory is wired up
by the top-level CMake configure step (core.hooksPath). `git commit --no-verify`
still bypasses it.
"""

import argparse
import os
import re
import sys

# ---- locations -------------------------------------------------------------

LUA_REL = os.path.join("scripts", "packet-analysis", "avclan_plugin.lua")
SRC_REL = "src"
SRC_EXTS = (".c", ".cc", ".h", ".hpp")

# Enum specs are matched to a Lua table by NAME; addresses are auto-detected
# separately (no Address enum exists yet). `style` picks the identifier casing,
# `hexfmt` the literal format for freshly added members (matched per file).
ENUM_SPECS = [
    {
        "name": "Action",
        "path": os.path.join("src", "avclan", "avclan.h"),
        "lua_table": "known_actions",
        "style": "action",
        "hexfmt": "0x%02x",
    },
    {
        "name": "Device",
        "path": os.path.join("src", "avclan", "device.hpp"),
        "lua_table": "known_devices",
        "style": "device",
        "hexfmt": "0x%02X",
    },
]

# Tokens kept fully uppercase when Title-casing an Action name, matching the
# existing hand-written style (CD_Enable_Repeat, Report_TOC). Note LAN -> Lan.
ACTION_ACRONYMS = {"CD", "TOC"}

# ---- name helpers ----------------------------------------------------------


def sanitize(name):
    """Convert an approximate identifier to a valid one: non-word chars become
    underscores and runs of underscores collapse to one."""
    s = re.sub(r"[^0-9A-Za-z_]", "_", name)
    s = re.sub(r"_+", "_", s)
    return s.strip("_")


def canonical(name):
    """Case/separator-insensitive key used to match names by identity."""
    return re.sub(r"[^0-9A-Za-z]", "", name).upper()


def style_device(name):
    return sanitize(name).upper()


def style_action(name):
    parts = [p for p in sanitize(name).split("_") if p]
    out = []
    for p in parts:
        if p.upper() in ACTION_ACRONYMS:
            out.append(p.upper())
        else:
            out.append(p[:1].upper() + p[1:].lower())
    return "_".join(out)


STYLERS = {"device": style_device, "action": style_action}

# ---- parsing ---------------------------------------------------------------

# An active Lua entry: `    [0x11] = "NAME",` with no leading `--`. The regex
# anchors `[` right after the indentation, so commented `-- [0x..]` lines and
# prose comment lines never match. The name is taken from inside the quotes,
# so trailing `-- ...` comments are ignored.
LUA_ENTRY_RE = re.compile(r'^\s*\[\s*0x([0-9A-Fa-f]+)\s*\]\s*=\s*"([^"]*)"')
LUA_TABLE_OPEN = "local %s = {"
LUA_TABLE_CLOSE_RE = re.compile(r"^\s*\}")

# A C++ enum member: `  Name = 0x11,` (or decimal). `//`-commented members are
# skipped by the caller, so they count as absent.
CPP_MEMBER_RE = re.compile(r"^\s*([A-Za-z_]\w*)\s*=\s*(0x[0-9A-Fa-f]+|\d+)")


class LuaEntry:
    def __init__(self, value, name, lineno):
        self.value = value
        self.name = name
        self.lineno = lineno


class CppMember:
    def __init__(self, value, name, idx):
        self.value = value
        self.name = name
        self.idx = idx  # 0-based index into the file's line list


def read_lines(path):
    with open(path, "r") as f:
        return f.readlines()


def parse_lua_table(lines, table_name):
    """Return the active entries of a Lua table, in file order."""
    open_marker = LUA_TABLE_OPEN % table_name
    start = None
    for i, line in enumerate(lines):
        if line.strip().startswith(open_marker):
            start = i
            break
    if start is None:
        return None
    entries = []
    for j in range(start + 1, len(lines)):
        line = lines[j]
        if LUA_TABLE_CLOSE_RE.match(line):
            break
        if line.lstrip().startswith("--"):
            continue
        m = LUA_ENTRY_RE.match(line)
        if m:
            entries.append(LuaEntry(int(m.group(1), 16), m.group(2), j + 1))
    return entries


def find_enum_body(lines, enum_name):
    """Locate `enum [macro] <name> ... {` and its closing brace. Returns
    (open_idx, close_idx) as line indices, or None."""
    open_re = re.compile(r"^\s*enum\b[^{;]*\b%s\b[^{;]*\{" % re.escape(enum_name))
    for i, line in enumerate(lines):
        if open_re.match(line):
            for j in range(i + 1, len(lines)):
                if LUA_TABLE_CLOSE_RE.match(lines[j]):  # `^\s*}` works for C++ too
                    return i, j
            return i, len(lines) - 1
    return None


def parse_cpp_enum(lines, enum_name):
    """Return (members, open_idx, close_idx, indent) for a C++ enum, ignoring
    commented-out members."""
    body = find_enum_body(lines, enum_name)
    if body is None:
        return None
    open_idx, close_idx = body
    members = []
    indent = "  "
    for j in range(open_idx + 1, close_idx):
        stripped = lines[j].lstrip()
        if stripped.startswith("//") or not stripped:
            continue
        m = CPP_MEMBER_RE.match(lines[j])
        if m:
            token = m.group(2)
            value = int(token, 16) if token.lower().startswith("0x") else int(token)
            members.append(CppMember(value, m.group(1), j))
            indent = lines[j][: len(lines[j]) - len(lines[j].lstrip())]
    return members, open_idx, close_idx, indent


def find_address_spec(root):
    """Auto-detect an `enum ... Address ... {` under src/, so addresses sync
    automatically once such an enum is introduced. None today."""
    addr_re = re.compile(r"^\s*enum\b[^{;]*\bAddress\b[^{;]*\{")
    for path in iter_src_files(root):
        if not path.endswith((".h", ".hpp")):
            continue
        with open(path, "r") as f:
            for line in f:
                if addr_re.match(line):
                    return {
                        "name": "Address",
                        "path": os.path.relpath(path, root),
                        "lua_table": "known_addresses",
                        "style": "device",
                        "hexfmt": "0x%03X",
                    }
    return None


def iter_src_files(root):
    for dirpath, _dirs, files in os.walk(os.path.join(root, SRC_REL)):
        for fn in sorted(files):
            if fn.endswith(SRC_EXTS):
                yield os.path.join(dirpath, fn)

# ---- lint ------------------------------------------------------------------


def lint_table(entries, path):
    """Return a list of (lineno, name, reason) for dissector names that break a
    hard rule."""
    problems = []
    seen = {}
    for e in entries:
        s = sanitize(e.name)
        if not s or s[0].isdigit():
            problems.append((e.lineno, e.name, "starts with a digit"))
            continue
        if len(s) < 3:
            problems.append((e.lineno, e.name, "shorter than 3 characters"))
        key = s.upper()
        if key in seen:
            problems.append((e.lineno, e.name,
                             "collides with '%s' (line %d) after sanitize"
                             % (seen[key][0], seen[key][1])))
        else:
            seen[key] = (e.name, e.lineno)
    return problems

# ---- reconciliation --------------------------------------------------------


class Drift:
    def __init__(self, spec):
        self.spec = spec
        self.renames = []   # (old_name, new_name, value)
        self.adds = []      # (value, new_name)
        self.cpp_only = []  # (value, name)

    def clean(self):
        return not (self.renames or self.adds or self.cpp_only)


def reconcile(spec, lua_entries, members):
    stylefn = STYLERS[spec["style"]]
    drift = Drift(spec)
    cpp_by_val = {m.value: m for m in members}
    lua_vals = set()
    for e in lua_entries:
        lua_vals.add(e.value)
        target = stylefn(e.name)
        if e.value in cpp_by_val:
            cur = cpp_by_val[e.value].name
            if canonical(cur) != canonical(e.name):
                drift.renames.append((cur, target, e.value))
        else:
            drift.adds.append((e.value, target))
    for m in members:
        if m.value not in lua_vals:
            drift.cpp_only.append((m.value, m.name))
    return drift

# ---- fix: rewrite enum bodies + references ---------------------------------


def compute_insertions(adds, lua_order, present_idx, close_idx, indent, hexfmt):
    """Map each added value to the line index it should be inserted before,
    mirroring the Lua table order. Returns {index: [rendered_line, ...]}."""
    groups = {}
    order_pos = {v: i for i, v in enumerate(lua_order)}
    for value, name in adds:  # adds are already in Lua order
        pos = order_pos[value]
        ins = None
        for j in range(pos - 1, -1, -1):
            if lua_order[j] in present_idx:
                ins = present_idx[lua_order[j]] + 1
                break
        if ins is None:
            for j in range(pos + 1, len(lua_order)):
                if lua_order[j] in present_idx:
                    ins = present_idx[lua_order[j]]
                    break
        if ins is None:
            ins = close_idx
        line = "%s%s = %s,\n" % (indent, name, hexfmt % value)
        groups.setdefault(ins, []).append(line)
    return groups


def apply_additions(path, spec, lua_entries, drift):
    if not drift.adds:
        return
    lines = read_lines(path)
    parsed = parse_cpp_enum(lines, spec["name"])
    members, _open_idx, close_idx, indent = parsed
    present_idx = {m.value: m.idx for m in members}
    lua_order = [e.value for e in lua_entries]
    groups = compute_insertions(drift.adds, lua_order, present_idx,
                                close_idx, indent, spec["hexfmt"])
    for ins in sorted(groups, reverse=True):
        lines[ins:ins] = groups[ins]
    with open(path, "w") as f:
        f.writelines(lines)


def replace_in_code(content, pattern, repl):
    """Apply `pattern` only to code, leaving comments, string literals and char
    literals untouched -- a bare token like a renamed enum member must not be
    rewritten where it merely appears in prose (e.g. `CSMA/CD` in a comment)."""
    out = []
    i = 0
    n = len(content)
    while i < n:
        c = content[i]
        nxt = content[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = content.find("\n", i)
            j = n if j == -1 else j
            out.append(content[i:j])
            i = j
        elif c == "/" and nxt == "*":
            j = content.find("*/", i + 2)
            j = n if j == -1 else j + 2
            out.append(content[i:j])
            i = j
        elif c == '"' or (c == "'" and not (i > 0 and (content[i - 1].isalnum()
                                                       or content[i - 1] == "_"))):
            # string, or a char literal (not a C++ digit separator like 1'000)
            quote = c
            j = i + 1
            while j < n:
                if content[j] == "\\":
                    j += 2
                    continue
                if content[j] == quote:
                    j += 1
                    break
                j += 1
            out.append(content[i:j])
            i = j
        else:
            j = i
            while j < n:
                cj = content[j]
                cj1 = content[j + 1] if j + 1 < n else ""
                if cj == '"':
                    break
                if cj == "'" and not (content[j - 1].isalnum() or content[j - 1] == "_"):
                    break
                if cj == "/" and (cj1 == "/" or cj1 == "*"):
                    break
                j += 1
            out.append(pattern.sub(repl, content[i:j]))
            i = j
    return "".join(out)


# A token right after one of these keywords is *introducing a name* (a type,
# enum, etc.), not referencing our enum member -- skip it so a member name that
# collides with an unrelated type (e.g. `enum CD { ... }`) is left alone.
DECL_KEYWORD_RE = re.compile(r"\b(?:enum|class|struct|union|namespace|typedef)\s+$")


def apply_renames(root, renames, verbose):
    """Replace whole-word code occurrences of every renamed member across src/.
    A single simultaneous pass (alternation) avoids A->B->C chaining."""
    if not renames:
        return
    mapping = {old: new for old, new, _v in renames}
    pattern = re.compile(r"\b(%s)\b" % "|".join(re.escape(o) for o in mapping))
    for path in iter_src_files(root):
        with open(path, "r") as f:
            content = f.read()
        count = [0]

        def repl(m):
            if DECL_KEYWORD_RE.search(m.string[: m.start()]):
                return m.group(0)  # a declaration, not a member reference
            count[0] += 1
            return mapping[m.group(1)]

        new_content = replace_in_code(content, pattern, repl)
        if new_content != content:
            with open(path, "w") as f:
                f.write(new_content)
            if verbose:
                print("  %s: %d replacement(s)" % (os.path.relpath(path, root), count[0]))

# ---- reporting -------------------------------------------------------------


def print_lint(spec, path, problems):
    for lineno, name, reason in problems:
        print("  %s:%d: %r -- %s" % (path, lineno, name, reason))


def print_drift(drift):
    spec = drift.spec
    for old, new, value in drift.renames:
        print("  rename %s -> %s  (0x%02X)" % (old, new, value))
    for value, name in drift.adds:
        print("  add    %s = 0x%02X" % (name, value))
    for value, name in drift.cpp_only:
        print("  ERROR  %s (0x%02X) is in C++ %s but not the dissector"
              % (name, value, spec["name"]))

# ---- driver ----------------------------------------------------------------


def build_specs(root):
    specs = list(ENUM_SPECS)
    addr = find_address_spec(root)
    if addr:
        specs.append(addr)
    return specs, addr is not None


def load(root, spec, lua_lines):
    entries = parse_lua_table(lua_lines, spec["lua_table"])
    if entries is None:
        sys.exit("error: Lua table '%s' not found" % spec["lua_table"])
    parsed = parse_cpp_enum(read_lines(os.path.join(root, spec["path"])), spec["name"])
    if parsed is None:
        sys.exit("error: C++ enum '%s' not found in %s" % (spec["name"], spec["path"]))
    return entries, parsed[0]


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fix", action="store_true",
                        help="apply the sync (rewrite enums + references)")
    parser.add_argument("--lint", action="store_true",
                        help="validate dissector names only")
    parser.add_argument("--verbose", action="store_true",
                        help="list every reference replacement made by --fix")
    parser.add_argument("--root",
                        help="repo root (default: inferred from this script)")
    args = parser.parse_args()

    root = args.root or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    lua_path = os.path.join(root, LUA_REL)
    lua_lines = read_lines(lua_path)
    specs, have_address = build_specs(root)
    if not have_address:
        print("note: no Address enum found in src/ -- addresses not synced")

    # ---- lint (all modes) ----
    lint_failed = False
    for spec in specs:
        entries = parse_lua_table(lua_lines, spec["lua_table"])
        if entries is None:
            sys.exit("error: Lua table '%s' not found" % spec["lua_table"])
        problems = lint_table(entries, LUA_REL)
        if problems:
            lint_failed = True
            print("dissector name problems in %s (%s):" % (spec["lua_table"], LUA_REL))
            print_lint(spec, LUA_REL, problems)

    if args.lint:
        if lint_failed:
            print("lint: FAILED")
            return 1
        print("lint: ok")
        return 0

    # ---- reconcile ----
    drifts = []
    for spec in specs:
        entries, members = load(root, spec, lua_lines)
        drifts.append((spec, entries, reconcile(spec, entries, members)))

    has_cpp_only = any(d.cpp_only for _s, _e, d in drifts)
    has_drift = any(not d.clean() for _s, _e, d in drifts)

    if args.fix:
        if lint_failed:
            print("fix aborted: resolve the dissector name problems above first")
            return 1
        if has_cpp_only:
            for _s, _e, d in drifts:
                if d.cpp_only:
                    print("%s:" % d.spec["name"])
                    print_drift(d)
            print("fix aborted: C++ has values the dissector lacks (see ERRORs)")
            return 1
        all_renames = []
        for spec, entries, drift in drifts:
            apply_additions(os.path.join(root, spec["path"]), spec, entries, drift)
            all_renames.extend(drift.renames)
        apply_renames(root, all_renames, args.verbose)
        for spec, entries, drift in drifts:
            if not drift.clean():
                print("%s: %d rename(s), %d addition(s)"
                      % (spec["name"], len(drift.renames), len(drift.adds)))
        print("fix: applied" if has_drift else "fix: already in sync")
        return 0

    # ---- default: report ----
    for spec, _entries, drift in drifts:
        if not drift.clean():
            print("%s drift:" % spec["name"])
            print_drift(drift)
    if lint_failed or has_drift:
        print("out of sync -- run: scripts/sync_avclan_enums.py --fix")
        return 1
    print("enums in sync with the dissector")
    return 0


if __name__ == "__main__":
    sys.exit(main())
