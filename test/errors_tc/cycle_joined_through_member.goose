// error: f calls into its recursive cycle while non-fixed-size local scratch
// f reaches a's cycle only through g, whose check has finished by then:
// calling g still leads back into the cycle, so f joins it, with scratch in
// scope (§7.8).
recursive fn a(n: i64) -> i64 {
    if n <= 0 { return 0; }
    let x = h(n);
    let y = f(n);
    return x + y;
}
fn h(n: i64) -> i64 { return g(n); }
fn g(n: i64) -> i64 { return a(n - 1); }
fn f(n: i64) -> i64 {
    var scratch: i64[>..] = [];
    scratch.push(n);
    return scratch[0] + g(n);
}
fn main() { print(a(12)); }
