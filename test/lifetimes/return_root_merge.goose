// A function's result is rooted as a branch's value is (§9.2): at the
// deepest of the roots its returns give, mapped to each call's own
// arguments, and exact only where every return names one root exactly. So a
// function may return a view of its input on one path and of a global arena,
// or a string literal, on another, and a caller may keep the result wherever
// the deepest of those allows.
var arena: u8[>..] = [];

struct Named { s: const u8[:] }
var names: Named[>..] = [];

// The caller's input on one path, the global arena on another.
fn spelling(original: const u8[:], generated: bool) -> const u8[:] {
    if generated {
        let begin = arena.len;
        arena.append("generated");
        return arena[begin..];
    }
    return original;
}

// A string literal or the arena: both outlive every caller, so the result
// may go into a global.
fn label(k: i64) -> const u8[:] {
    if k < 0 { return "negative"; }
    let begin = arena.len;
    arena.append("#");
    arena.push(48 + k as u8);
    return arena[begin..];
}

// A literal or a parameter.
fn or_default(s: const u8[:]) -> const u8[:] {
    if s.len == 0 { return "(empty)"; }
    return s;
}

// Either of two parameters: rooted at whichever argument is deeper at each
// call, whatever order they come in.
fn longer(a: const u8[:], b: const u8[:]) -> const u8[:] {
    if a.len >= b.len { return a; }
    return b;
}

// A holder built from a literal on one path and from a parameter on another.
struct CValue { code: const u8[:], ty: i64 }
fn cvalue(s: const u8[:], k: i64) -> CValue {
    if k == 0 { return CValue { code: "none", ty: 0 }; }
    return CValue { code: s, ty: k };
}

// Several results, each merged over the returns on its own.
fn cut(s: const u8[:], at: i64) -> const u8[:], const u8[:] {
    if at <= 0 { return "", s; }
    if at >= s.len { return s, label(at); }
    return s[..at], s[at..];
}

fn main() {
    var input: u8[>..] = "source";
    print(spelling(input, false));
    print(spelling(input, true));
    print(or_default(""));
    print(or_default(input));
    names.push(Named { s: label(-1) });
    names.push(Named { s: label(7) });
    for n in names { print(n.s); }

    var x: u8[>..] = "abc";
    var kept: Named[>..] = [];
    {
        var y: u8[>..] = "defgh";
        var inner: Named[>..] = [];
        inner.push(Named { s: longer(x, y) });
        inner.push(Named { s: longer(y, x) });
        for n in inner { print(n.s); }
        // Rooted at y either way, so kept, which outlives y, may not hold
        // them, but x's own views it may.
        kept.push(Named { s: longer(x, x) });
    }
    // Two arguments declared side by side tie: either is the bound.
    var z: u8[>..] = "zz";
    kept.push(Named { s: longer(x, z) });
    kept.push(Named { s: longer(z, x) });
    for n in kept { print(n.s); }

    var cs: CValue[>..] = [];
    cs.push(cvalue(input, 0));
    cs.push(cvalue(input, 2));
    for c in cs { print(c.code, " ", c.ty); }

    let head, tail = cut(input, 2);
    print(head, "|", tail);
    let front, back = cut(x, 0);
    print(front, "|", back);
    let whole, lab = cut(z, 5);
    print(whole, "|", lab);
}
