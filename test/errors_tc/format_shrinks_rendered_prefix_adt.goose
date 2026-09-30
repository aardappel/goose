// error: cannot call format, which shrinks s: print runs it in the middle of rendering s, which may refer into s (§5.1)
// It rebuilds a variable-mode ADT in the value too, resizable or not: r's
// variant is replaced while its payload is rendered (§3.7, §4.4, §5.1).
struct A { v: i64 }
struct Q { x: i64, y: i64 }
enum R {
    Big { a: A, q: Q& },
    Small { w: i64, h: i64, k: i64 },
}
struct S { r: R.., items: i64[>..] }
var q1 = Q { x: 1, y: 2 };
var s = S { r: R.Small { w: 0, h: 0, k: 0 }, items: [] };
fn format(out: u8[>..]&, a: A) {
    s = S { r: R.Small { w: 4096, h: 100000, k: 3 }, items: [7] };
    format(out, a.v);
}
fn main() {
    s = S { r: R.Big { a: A { v: 5 }, q: q1 }, items: [1, 2] };
    print(s);
}
