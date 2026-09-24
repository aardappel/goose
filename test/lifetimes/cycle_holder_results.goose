// A recursive cycle's result may hold references (§7.8, §9.2): a struct with
// slice fields is predicted and checked by the roots of what it holds, as a
// returned reference is. The first return checked stands in for the
// prediction, inexact: a field read out of a back edge's holder re-derives
// its root (§9.5), and the returns built from it are no more exact than that.

struct Fragment { text: const u8[:] }

// Each level's text is saved in a global arena, which outlives every
// activation. A level forwards its child's text in a new holder, or returns
// the child itself.
var arena: u8[>..] = [];
fn saved(s: const u8[:]) -> Fragment {
    let begin = arena.len;
    arena.append(s);
    return Fragment { text: arena[begin..] };
}
recursive fn forwarded(n: i64) -> Fragment {
    if n == 0 { return saved("leaf"); }
    let child = forwarded(n - 1);
    return Fragment { text: child.text[1..] };
}
recursive fn deepest(n: i64) -> Fragment {
    if n == 0 { return saved("deepest"); }
    let child = deepest(n - 1);
    return child;
}

// A string literal first, the arena as well later: a root a later return
// adds joins what the back edges after it are given.
recursive fn mixed(n: i64) -> Fragment {
    if n == 0 { return Fragment { text: "static" }; }
    let child = mixed(n - 1);
    if n % 2 == 0 { return saved(str("<", child.text, ">")); }
    return Fragment { text: child.text };
}

// Views of the input, a slice parameter's or a holder parameter's: the back
// edge maps the parameter's root through the argument it passes.
recursive fn letter(s: const u8[:], n: i64) -> Fragment {
    if n == 0 || s.len == 1 { return Fragment { text: s[..1] }; }
    let child = letter(s[1..], n - 1);
    return Fragment { text: child.text };
}
recursive fn tail(h: Fragment, n: i64) -> Fragment {
    if n == 0 { return h; }
    let r = tail(Fragment { text: h.text[1..] }, n - 1);
    return r;
}

// Several slice fields, two of them defaulted, and helpers outside the cycle
// that take one by value and return one, applied to a back edge's result.
struct Value {
    code: const u8[:],
    ty: i64,
    address: const u8[:] = "",
    stack: const u8[:] = "",
}
fn named(name: const u8[:], ty: i64) -> Value {
    return Value { code: saved(name).text, ty: ty, address: saved(str("&", name)).text };
}
fn converted(v: Value, ty: i64) -> Value {
    return Value { code: saved(str("(T", ty, ")", v.code)).text, ty: ty, address: v.address,
                   stack: v.stack };
}
fn snapshot(v: Value) -> Value {
    if v.ty == 0 { return v; }
    return Value { code: saved(str("t", v.ty)).text, ty: v.ty, address: v.address,
                   stack: saved(str("s", v.ty)).text };
}
recursive fn lowered(n: i64) -> Value {
    if n == 0 { return named("v", 1); }
    let a = snapshot(converted(lowered(n - 1), n + 1));
    if n == 1 { return Value { code: a.code, ty: a.ty }; }
    return Value { code: saved(str(a.code, "+1")).text, ty: a.ty, address: a.address,
                   stack: a.stack };
}

// Mutual recursion: the entry's first return stands in for its prediction
// before the function it recurses through calls back.
recursive fn outer(n: i64) -> Fragment {
    if n == 0 { return saved("o"); }
    let i = inner(n - 1);
    if n % 2 == 0 { return Fragment { text: i.text }; }
    return saved(str("o", i.text));
}
fn inner(n: i64) -> Fragment {
    let o = outer(n);
    return Fragment { text: o.text[..1] };
}

fn main() {
    print(forwarded(3).text);
    print(deepest(4).text);
    print(mixed(3).text, " ", mixed(4).text);
    print(letter("goose", 2).text, " ", letter("goose", 9).text);
    var word: u8[>..] = "gander";
    print(tail(Fragment { text: word[..] }, 4).text);
    let v = lowered(2);
    print(v.code, " ", v.ty, " [", v.address, "] [", v.stack, "]");
    print(outer(3).text);
}
