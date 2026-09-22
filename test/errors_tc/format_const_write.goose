// error: read-only
struct S { x: i64 }
fn format(out: u8[>..]&, v: S&) { v.x = 99; format(out, v.x); }
fn main() {
    var a = S { 1 };
    const b = S { 2 };
    print(a, b);
}
