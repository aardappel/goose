// error: cannot modify through this path (const, or a read-only instantiation
// A var of a const struct type takes a copy of a plain struct and is
// assigned as a whole, while its fields stay read-only: ++ writes one (§9.5).
struct S { n: i64 }
fn main() {
    let s = S { n: 1 };
    var d: const S = s;
    d = S { n: 2 };
    d.n++;
    print(d.n);
}
