// error: storing a reference rooted at <temporary>, which does not outlive the destination
// A [] passed to a function value's slice parameter is a temporary (§4.2),
// as a literal with elements is (literal_function_value_store).
struct Keep { s: const i64[:] }
var kept = default<Keep>();
fn run<F>() { F([]); }
fn main() {
    run() { v: const i64[:] => kept.s = v; };
    print(kept.s.len);
}
