// error: a is not fixed-size and is not copied implicitly
// A loop that is a function's final expression gives the function's result
// by `break`; its body's own final expression is a statement, which `a`
// outlives, so a copy of `a` passed there takes copy() (§4.1).
fn total(xs: i64[>..]) -> i64 { xs.len }
fn count(c: bool) -> i64 {
    var a: i64[>..] = [1];
    var n = 0;
    loop {
        n += 1;
        if n > 2 { break n; }
        total(if c { a } else { a })
    }
}
fn main() { print(count(true)); }
