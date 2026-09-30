// error: cannot assign through this path (const, or a read-only instantiation
// A format overload taking a value by reference is handed what a reference
// argument names, as writable as the reference is (§3.7, §9.5): through a
// `const S&` it is read-only, so an overload writing it is rejected.
struct S { x: i64 }
fn format(out: u8[>..]&, v: S&) { v.x = 7; out.append("S"); }
fn show(r: const S&) { print(r); }
fn main() {
    var s = S { x: 1 };
    show(s);
    print(s.x);
}
