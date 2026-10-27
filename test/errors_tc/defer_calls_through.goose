// error: defer body calls helper, which uses a long-distance return (a `return ... from` site, a frame one passes through, or a catch, §7.9): a defer body runs while such a return may be in flight (§6.6)
fn fail(code: i64) { return code from run; }
fn helper(n: i64) { if n > 100 { fail(n); } }
fn step(n: i64) -> i64 {
    defer helper(n);
    return n;
}
fn run(n: i64) -> i64 {
    return step(n);
}
fn main() { print(run(1)); }
