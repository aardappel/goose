// runtime-debug: a variable-mode ADT's payload and an array's elements are
// rendered where they lie, by the variant and the count read as each one's
// rendering begins (§3.7). The overloads of their parts may grow and shrink
// other storage, and an overload rendering a part beside them may replace
// the variant or rebuild the array, before the rendering reaches it or after
// it is done: the rendering reads what is there when it gets there.
struct B { v: i64 }
struct T { v: i64 }
struct Q { x: i64, y: i64 }
enum R {
    Big { b: B, q: Q&, items: i64[>..<] },
    Small { w: i64, h: i64, k: i64 },
}
struct S { t: T, names: B[], items: B[>..<] }
struct Before { t: T, r: R..& }
struct After { r: R..&, t: T }
struct Rebuilt { t: T, s: S& }
struct Held { r: R..& }
var q1 = Q { x: 1, y: 2 };
var G: R.. = R.Small { w: 0, h: 0, k: 0 };
var other: R.. = R.Small { w: 0, h: 0, k: 0 };
var s = S { t: T { v: 1 }, names: [B { v: 10 }, B { v: 11 }], items: [B { v: 12 }] };
var seen: i64[>..] = [];

// Inside G's payload and s's arrays: other storage only.
fn format(out: u8[>..]&, b: B) {
    seen.push(b.v);
    other = R.Big { b: B { v: b.v }, q: q1, items: [seen.len] };
    out.append("B");
    format(out, b.v);
}

// Beside them, s's own t included: G's variant and all of s.
fn format(out: u8[>..]&, t: T) {
    G = R.Small { w: t.v, h: 2, k: 3 };
    s = S { t: T { v: t.v + 1 }, names: [B { v: t.v * 10 }], items: [B { v: t.v * 10 + 1 }] };
    out.append("T");
    format(out, t.v);
}

// What a caller's storage holds is walked where it points: G, never other.
fn show(h: Held&) { print(h); }
fn show1(r: R..&) { print(r); }
fn show2(h: Held&) { show1(h.r); }

fn main() {
    G = R.Big { b: B { v: 5 }, q: q1, items: [1, 2] };
    print(G);
    print(s);
    print(Before { t: T { v: 7 }, r: G });
    G = R.Big { b: B { v: 6 }, q: q1, items: [3] };
    print(After { r: G, t: T { v: 8 } });
    print(G);
    print(Rebuilt { t: T { v: 9 }, s: s });
    let text = str(G, " ", s);
    print(text);
    G = R.Big { b: B { v: 4 }, q: q1, items: [] };
    var held = Held { r: G };
    show(held);
    show2(held);
    print(seen);
}
