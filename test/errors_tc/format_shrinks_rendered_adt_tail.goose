// error: cannot call format, which shrinks s: format runs it in the middle of rendering s, which may refer into s (§5.1)
// A resizable struct whose tail is such an ADT: assigning the tail replaces
// its variant while its payload is rendered (§3.7, §4.4, §5.1).
struct A { v: i64 }
struct Q { x: i64, y: i64 }
enum R {
    Big { a: A, q: Q&, items: i64[>..] },
    Small { w: i64, h: i64, k: i64 },
}
struct S { n: i64, r: R.. }
var q1 = Q { x: 1, y: 2 };
var s = S { n: 0, r: R.Small { w: 0, h: 0, k: 0 } };
fn format(out: u8[>..]&, a: A) {
    s.r = R.Small { w: 4096, h: 100000, k: 3 };
    format(out, a.v);
}
fn main() {
    s.r = R.Big { a: A { v: 5 }, q: q1, items: [1, 2] };
    var text: u8[>..] = [];
    format(text, s);
    print(text);
}
