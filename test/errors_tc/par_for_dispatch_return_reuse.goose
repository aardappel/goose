// error: return ... from out of a par for
// The case functions of invoke are checked first where F's return from h
// stays in the outer loop's body, then called again from a par for in wrap,
// out of which the same return would leave (docs/design/child_threads.md
// §4.3 R4).
enum E { A, B }
fn invoke<F>(e: E.A, x: i64) { F(); }
fn invoke<F>(e: E.B, x: i64) { F(); }
fn wrap<F>(e: E, x: i64) -> i64 {
    invoke(e, x, F);
    par for k in 2 { invoke(e, x, F); }
    return 0;
}
fn h(e: E, x: i64) -> i64 {
    wrap(e, x) { if x > 100 { return 3 from h; } };
    return 0;
}
fn main() {
    let e: E = E.A;
    var out: i64[1] = [0];
    par for o in 1 { out[o] = h(e, 1); }
    print(out);
}
