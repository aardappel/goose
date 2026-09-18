"""Checks that stdlib/gfx.goose and src/gfx/gfx_api.h describe the same
boundary: every `extern "gs_gfx_..." fn` against GS_GFX_API, parameter by
parameter; every struct that crosses, field by field; and every constant in
GS_GFX_CONSTANTS. A mismatch there compiles on both sides and then passes
garbage, which is why it is checked rather than trusted.

The naming convention the check relies on: the Goose struct `TextureDesc` is
`gs_gfx_texture_desc` in C, a slice `const Buffer[:]` is
`gs_gfx_buffer_slice`, `u8[:]` is `gs_gfx_bytes`, `T&` is a pointer.

Used by run_tests.py: check() returns a list of problems, empty when the two
agree. Runnable alone: python test/gfx_api_check.py
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PRIMS = {"bool": "uint8_t", "i8": "int8_t", "i16": "int16_t", "i32": "int32_t",
         "i64": "int64_t", "u8": "uint8_t", "u16": "uint16_t", "u32": "uint32_t",
         "u64": "uint64_t", "f32": "float", "f64": "double"}


def snake(name):
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()


def c_of(goose_type):
    """The C spelling a Goose parameter or field type crosses as."""
    t = goose_type.strip()
    if t.startswith("const "):
        t = t[len("const "):].strip()
    if t.endswith("&"):
        return c_of(t[:-1]) + " *"
    if t == "u8[:]":
        return "gs_gfx_bytes"
    if t.endswith("[:]"):
        return f"gs_gfx_{snake(t[:-3])}_slice"
    if t in PRIMS:
        return PRIMS[t]
    return f"gs_gfx_{snake(t)}"


def strip_comments(text, style):
    if style == "c":
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def split_top(text, sep=","):
    """Splits at `sep` outside brackets, braces and parentheses."""
    parts, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return [p.strip() for p in parts if p.strip()]


def c_param(p):
    """A C parameter without its name, spaced as c_of spells it."""
    p = re.sub(r"\bconst\b", "", p).strip()
    m = re.match(r"^([\w ]+?)\s*(\*?)\s*\w+$", p)
    if not m:
        return p
    return m.group(1).strip() + (" *" if m.group(2) else "")


def parse_c(text):
    text = strip_comments(text, "c")
    consts = {m[1]: (m[0], int(m[2]))
              for m in re.findall(r"X\((i32|u8), (\w+), (-?\d+)\)", text)}
    fns = {}
    body = text[text.index("#define GS_GFX_API(X)"):text.index("#define GS_GFX_PROTO")]
    for ret, name, params in re.findall(r"X\(([\w ]+?), (gs_gfx_\w+), \((.*?)\)\)", body):
        ps = [] if params.strip() == "void" else [c_param(p) for p in split_top(params)]
        fns[name] = (ret.strip(), ps)
    structs = {}
    for fields, name in re.findall(r"typedef struct \{(.*?)\} (gs_gfx_\w+);", text, re.S):
        out = []
        for decl in fields.split(";"):
            decl = decl.strip()
            if not decl:
                continue
            m = re.match(r"^([\w ]+?\*?)\s+(.*)$", decl)
            ctype, names = m.group(1).strip(), m.group(2)
            for n in names.split(","):
                n = n.strip()
                ptr = n.startswith("*")
                n = n.lstrip("*").strip()
                count = 1
                am = re.match(r"^(\w+)\[(\d+)\]$", n)
                if am:
                    n, count = am.group(1), int(am.group(2))
                out.append((n, ctype + (" *" if ptr else ""), count))
        structs[name] = out
    return consts, fns, structs


def parse_goose(text):
    text = strip_comments(text, "goose")
    consts = {m[0]: (m[1], int(m[2]))
              for m in re.findall(r"^let (\w+): (i32|u8) = (-?\d+);", text, re.M)}
    fns = {}
    for sym, name, params, ret in re.findall(
            r'extern "(gs_gfx_\w+)" fn (\w+)\((.*?)\)(?:\s*->\s*([^;{]+?))?\s*;', text, re.S):
        ps = []
        for p in split_top(params):
            _, _, t = p.partition(":")
            ps.append(c_of(t))
        fns[sym] = (c_of(ret) if ret.strip() else "void", ps, name)
    structs = {}
    for m in re.finditer(r"^struct (\w+)\s*\{", text, re.M):
        start = m.end()
        depth, i = 1, start
        while depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        out = []
        for f in split_top(text[start:i - 1]):
            if f.startswith("pad"):
                out.append(("pad", f, 1))
                continue
            name, _, rest = f.partition(":")
            t = rest.split("=")[0].strip()
            count = 1
            am = re.match(r"^(.*)\[(\d+)\]$", t)
            if am:
                t, count = am.group(1), int(am.group(2))
            out.append((name.strip(), c_of(t), count))
        structs[f"gs_gfx_{snake(m.group(1))}"] = out
    return consts, fns, structs


def check():
    ctext = (REPO / "src" / "gfx" / "gfx_api.h").read_text(encoding="utf-8")
    gtext = (REPO / "stdlib" / "gfx.goose").read_text(encoding="utf-8")
    cconsts, cfns, cstructs = parse_c(ctext)
    gconsts, gfns, gstructs = parse_goose(gtext)
    problems = []
    for name in sorted(set(cfns) | set(gfns)):
        if name not in gfns:
            problems.append(f"{name} is in gfx_api.h and not declared in gfx.goose")
        elif name not in cfns:
            problems.append(f"{name} is declared in gfx.goose and not in gfx_api.h")
        else:
            cret, cps = cfns[name]
            gret, gps, gname = gfns[name]
            if cret != gret or cps != gps:
                problems.append(f"{name} (gfx::{gname}): C has {cret} ({', '.join(cps)}), "
                                f"Goose crosses as {gret} ({', '.join(gps)})")
    # Every struct a declaration passes, and every one gfx_api.h declares,
    # must agree; int2/float2/float4 are vec's, checked by name alone.
    for name, cfields in cstructs.items():
        if name in ("gs_gfx_bytes", "gs_gfx_int2", "gs_gfx_float2", "gs_gfx_float4") or \
                name.endswith("_slice"):
            continue
        gfields = gstructs.get(name)
        if gfields is None:
            problems.append(f"struct {name} has no Goose counterpart in gfx.goose")
        elif gfields != cfields:
            problems.append(f"struct {name}: C fields {cfields}, Goose fields {gfields}")
    for name in sorted(set(cconsts) | set(gconsts)):
        if cconsts.get(name) != gconsts.get(name):
            problems.append(f"constant {name}: gfx_api.h {cconsts.get(name)}, "
                            f"gfx.goose {gconsts.get(name)}")
    if not cfns or not cconsts or not cstructs:
        problems.append("nothing parsed out of gfx_api.h")
    return problems


if __name__ == "__main__":
    found = check()
    for p in found:
        print(p)
    print("gfx api: " + ("mismatch" if found else "gfx_api.h and gfx.goose agree"))
    sys.exit(1 if found else 0)
