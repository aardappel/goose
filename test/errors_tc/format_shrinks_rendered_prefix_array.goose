// error: cannot call format, which shrinks s: print runs it in the middle of rendering s, which may refer into s (§5.1)
// A whole assignment rebuilds every variable-size part of a resizable
// value, not just its tail: names, counted as four before the first
// element's overload, holds one after it, and the rest of the count would
// be read out of items (§3.7, §4.4, §5.1).
struct A { v: i64 }
struct S { names: A[], items: i64[>..] }
var s = S { names: [A { v: 1 }], items: [] };
fn format(out: u8[>..]&, a: A) {
    s = S { names: [A { v: 9 }], items: [123456789, 5] };
    format(out, a.v);
}
fn main() {
    s = S { names: [A { v: 1 }, A { v: 2 }, A { v: 3 }, A { v: 4 }], items: [1, 2] };
    print(s);
}
