// error: cannot call format, which shrinks b while s is still used
// format hands its receiver to the overload as the builder, which the
// overload clears before s, a view of it, is appended (§3.7, §5.1).
struct C { n: i64 }
fn format(out: u8[>..]&, c: C) {
    out.clear();
    out.append("C");
}
fn main() {
    var b: u8[>..] = [];
    b.append("abc");
    let s = b[..];
    b.format(C { 1 }, s);
    print(b);
}
