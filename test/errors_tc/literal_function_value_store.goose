// error: storing a reference rooted at <temporary>, which does not outlive the destination
// An array literal passed to a function value's slice parameter is a
// temporary (§9.2).
struct Keep { s: const i64[:] }
var kept = default<Keep>();
fn run<F>() { F([1, 2, 3]); }
fn main() {
    run() { v: const i64[:] => kept.s = v; };
    print(kept.s[0]);
}
