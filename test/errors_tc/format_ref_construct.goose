// error: cannot assign through this path (const, or a read-only instantiation
// An `if` with no destination type gives a copy of the branch taken (§4.1),
// even where the branches are references: a format overload taking the
// value by reference is handed that temporary, read-only (§3.7, §9.5), not
// what the references name.
struct S { x: i64 }
fn format(out: u8[>..]&, v: S&) { v.x = 7; out.append("S"); }
fn pick(a: S&, b: S&, c: bool) { print(if c { a } else { b }); }
fn main() {
    var s = S { x: 1 };
    var t = S { x: 2 };
    pick(s, t, true);
    print(s.x);
}
