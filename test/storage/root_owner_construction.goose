// runtime-debug: calls during construction grow only the other component.
root struct S { first: u8[][>..], second: u8[][>..] }
fn other(s: S&) -> u8[] { s.second.push("other"); return "first"; }
fn build(s: S&) { s.first.push(other(s)); }
fn separate(a: S&, b: S&) { a.first.push(other(b)); }
fn main() {
    var a = S { .. };
    var b = S { .. };
    build(a);
    separate(a, a);
    separate(a, b);
    print(a.first, " ", a.second, " ", b.second);
}
