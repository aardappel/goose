// error: defer body calls fail, which uses a long-distance return (a `return ... from` site, a frame one passes through, or a catch, §7.9): a defer body runs while such a return may be in flight (§6.6)
// A defer body runs on the propagation path of a long-distance return, so
// it may not perform one.
fn fail(code: i64) { return code from run; }
fn step(n: i64) -> i64 {
    defer fail(n);
    return n;
}
fn run(n: i64) -> i64 {
    return step(n);
}
fn main() { print(run(1)); }
