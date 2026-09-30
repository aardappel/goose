// error: cannot modify through this path (const, or a read-only instantiation
// A var of a const struct type is read-only through a reference parameter:
// bump's instantiation for it is (§9.5).
struct S { n: i64 }
fn bump(s: S&) { s.n++; }
fn main() {
    var d: const S = S { n: 1 };
    bump(d);
    print(d.n);
}
