// error: cannot call format, which shrinks s: print runs it in the middle of rendering r, which may refer into s (§5.2)
// A resizable struct's tail array is rendered over the elements it counted
// before the first element's overload, which clears it (§3.7, §5.2).
struct A { v: i64 }
struct S { n: i64, items: A[>..<] }
var s = S { n: 1, items: [A { v: 1 }, A { v: 2 }] };
fn format(out: u8[>..]&, a: A) {
    s.items.clear();
    format(out, a.v);
}
fn main() {
    let r: S& = s;
    print(r);
}
