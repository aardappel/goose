"""Checks that each stdlib module with a native layer and that layer's C API
header describe the same boundary: stdlib/gfx.goose against
src/gfx/gfx_api.h, and stdlib/physics.goose against
src/physics/physics_api.h. Every `extern "gs_..." fn` against the header's
list of functions, parameter by parameter; every struct that crosses, field
by field; and every constant. A mismatch there compiles on both sides and
then passes garbage, which is why it is checked rather than trusted.

It also checks what the header passes by value is what TinyCC passes the way
the C compilers do: TinyCC classifies a struct of up to 16 bytes as a whole,
where the System V x86-64 ABI classifies each eightbyte, so such a struct
mixing floats and integers would arrive in different registers.

The naming convention the check relies on: the Goose struct `TextureDesc` is
`gs_gfx_texture_desc` in C, a slice `const Buffer[:]` is
`gs_gfx_buffer_slice`, `u8[:]` is `gs_gfx_bytes`, `T&` is a pointer; the
same with `gs_phys_` for physics.

Used by run_tests.py: check(module) returns a list of problems, empty when the
two agree. Runnable alone: python test/api_check.py [gfx|physics]
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PRIMS = {"bool": "uint8_t", "i8": "int8_t", "i16": "int16_t", "i32": "int32_t",
         "i64": "int64_t", "u8": "uint8_t", "u16": "uint16_t", "u32": "uint32_t",
         "u64": "uint64_t", "f32": "float", "f64": "double"}
C_SIZES = {"uint8_t": 1, "int8_t": 1, "int16_t": 2, "uint16_t": 2, "int32_t": 4,
           "uint32_t": 4, "int64_t": 8, "uint64_t": 8, "float": 4, "double": 8}
# The vec module's types: declared on both sides, checked by name alone.
VECS = ("int2", "int3", "float2", "float3", "float4")

# Each module: its header, its Goose file, the prefix of its list macros and
# of its C names.
MODULES = {
    "gfx": ("src/gfx/gfx_api.h", "stdlib/gfx.goose", "GFX", "gs_gfx_"),
    "physics": ("src/physics/physics_api.h", "stdlib/physics.goose", "PHYS", "gs_phys_"),
}


def snake(name):
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()


def c_of(goose_type, prefix):
    """The C spelling a Goose parameter or field type crosses as."""
    t = goose_type.strip()
    if t.startswith("const "):
        t = t[len("const "):].strip()
    if t.endswith("&"):
        return c_of(t[:-1], prefix) + " *"
    if t == "u8[:]":
        return f"{prefix}bytes"
    if t.endswith("[:]"):
        return f"{prefix}{snake(t[:-3])}_slice"
    if t in PRIMS:
        return PRIMS[t]
    return f"{prefix}{snake(t)}"


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


def parse_c(text, macro, prefix):
    text = strip_comments(text, "c")
    consts = {m[1]: (m[0], int(m[2]))
              for m in re.findall(r"X\((i32|u8), (\w+), (-?\d+)\)", text)}
    fns = {}
    body = text[text.index(f"#define GS_{macro}_API(X)"):text.index(f"#define GS_{macro}_PROTO")]
    for ret, name, params in re.findall(r"X\(([\w ]+?), (" + prefix + r"\w+), \((.*?)\)\)", body,
                                        re.S):
        ps = [] if params.strip() == "void" else [c_param(p) for p in split_top(params)]
        fns[name] = (ret.strip(), ps)
    structs = {}
    for fields, name in re.findall(r"typedef struct \{(.*?)\} (" + prefix + r"\w+);", text, re.S):
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


def parse_goose(text, prefix):
    text = strip_comments(text, "goose")
    consts = {m[0]: (m[1], int(m[2]))
              for m in re.findall(r"^let (\w+): (i32|u8) = (-?\d+);", text, re.M)}
    fns = {}
    for sym, name, params, ret in re.findall(
            r'extern "(' + prefix + r'\w+)" fn (\w+)\((.*?)\)(?:\s*->\s*([^;{]+?))?\s*;', text, re.S):
        ps = []
        for p in split_top(params):
            _, _, t = p.partition(":")
            ps.append(c_of(t, prefix))
        fns[sym] = (c_of(ret, prefix) if ret.strip() else "void", ps, name)
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
            out.append((name.strip(), c_of(t, prefix), count))
        structs[f"{prefix}{snake(m.group(1))}"] = out
    return consts, fns, structs


def scalars(ctype, structs, offset=0):
    """The scalars a packed C type is made of, as (offset, size, kind), kind
    'f' for floating point and 'i' for integers and pointers; None for a
    type not declared in the header."""
    if ctype.endswith("*"):
        return [(offset, 8, "i")]
    if ctype in C_SIZES:
        return [(offset, C_SIZES[ctype], "f" if ctype in ("float", "double") else "i")]
    if ctype not in structs:
        return None
    out = []
    for _, ftype, count in structs[ctype]:
        for _ in range(count):
            inner = scalars(ftype, structs, offset)
            if inner is None:
                return None
            out += inner
            offset = inner[-1][0] + inner[-1][1] if inner else offset
    return out


def abi_problem(ctype, structs):
    """Why TinyCC would pass a struct of this type by value differently from
    the C compilers on System V x86-64, or None. A struct over 16 bytes goes
    in memory everywhere. Below that, the ABI puts a struct with a misaligned
    field in memory, and gives each eightbyte its own class, where TinyCC
    goes by the whole struct: integers anywhere make all of it integer."""
    parts = scalars(ctype, structs)
    if not parts:
        return None
    size = parts[-1][0] + parts[-1][1]
    if size > 16:
        return None
    if any(off % n for off, n, _ in parts):
        return f"{size} bytes with a misaligned field"
    halves = [{k for off, _, k in parts if off < 8}, {k for off, _, k in parts if off >= 8}]
    if "i" in halves[0] | halves[1] and size > 8 and any(h == {"f"} for h in halves):
        return f"{size} bytes, one eightbyte all floats and the other not"
    return None


def check(module):
    header, goose, macro, prefix = MODULES[module]
    ctext = (REPO / header).read_text(encoding="utf-8")
    gtext = (REPO / goose).read_text(encoding="utf-8")
    cconsts, cfns, cstructs = parse_c(ctext, macro, prefix)
    gconsts, gfns, gstructs = parse_goose(gtext, prefix)
    hname, gname_file = Path(header).name, Path(goose).name
    problems = []
    for name in sorted(set(cfns) | set(gfns)):
        if name not in gfns:
            problems.append(f"{name} is in {hname} and not declared in {gname_file}")
        elif name not in cfns:
            problems.append(f"{name} is declared in {gname_file} and not in {hname}")
        else:
            cret, cps = cfns[name]
            gret, gps, gname = gfns[name]
            if cret != gret or cps != gps:
                problems.append(f"{name} ({module}::{gname}): C has {cret} ({', '.join(cps)}), "
                                f"Goose crosses as {gret} ({', '.join(gps)})")
    # Every struct a declaration passes, and every one the header declares,
    # must agree; the vec types are checked by name alone.
    for name, cfields in cstructs.items():
        if name == f"{prefix}bytes" or name.endswith("_slice") or \
                name[len(prefix):] in VECS:
            continue
        gfields = gstructs.get(name)
        if gfields is None:
            problems.append(f"struct {name} has no Goose counterpart in {gname_file}")
        elif gfields != cfields:
            problems.append(f"struct {name}: C fields {cfields}, Goose fields {gfields}")
    for name in sorted(set(cconsts) | set(gconsts)):
        if cconsts.get(name) != gconsts.get(name):
            problems.append(f"constant {name}: {hname} {cconsts.get(name)}, "
                            f"{gname_file} {gconsts.get(name)}")
    for name, (ret, params) in sorted(cfns.items()):
        for what, ctype in [("returns", ret)] + [("takes", p) for p in params]:
            why = abi_problem(ctype, cstructs) if ctype in cstructs else None
            if why:
                problems.append(f"{name} {what} {ctype} by value, which TinyCC passes "
                                f"differently: {why}")
    if not cfns or not cconsts or not cstructs:
        problems.append(f"nothing parsed out of {hname}")
    return problems


if __name__ == "__main__":
    status = 0
    for module in sys.argv[1:] or MODULES:
        found = check(module)
        for p in found:
            print(p)
        print(f"{module} api: " + ("mismatch" if found else
                                   f"{Path(MODULES[module][0]).name} and "
                                   f"{Path(MODULES[module][1]).name} agree"))
        status |= 1 if found else 0
    sys.exit(status)
