// error: cannot assign through this path (const, or a read-only instantiation
// A format overload taking its value by reference is handed a const value
// read-only (§3.7, §9.5), so rendering b rejects its write to v.x.
struct S { x: i64 }
fn format(out: u8[>..]&, v: S&) { v.x = 99; format(out, v.x); }
fn main() {
    var a = S { 1 };
    const b = S { 2 };
    print(a, b);
}
