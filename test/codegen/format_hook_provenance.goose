struct S { x: i64 }
fn format(out: u8[>..]&, v: S&) { v.x++; format(out, v.x); }
struct R { x: i64 }
fn format(out: u8[>..]&, v: const R&) { format(out, v.x); }
fn main() {
    var a = S { 1 };
    var b = S { 10 };
    print(a, " ", b);
    var text: u8[>..] = [];
    format(text, b, " ", a);
    print(text);
    print(str(a, " ", b));
    const r = R { 42 };
    print(r, " ", [r, r]);
}
