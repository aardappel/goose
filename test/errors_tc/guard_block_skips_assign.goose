// error: variable n may be used before it is assigned
// The guard in the block skips the rest of the block, where n is assigned,
// and nothing more: once runs the block exactly once (§4.4, §6.4, §7.6).
fn once<F>() { F(); }
fn run(c: bool) {
    var n: i64;
    once() { guard c; n = 5; }
    print(n);
}
fn main() { run(true); run(false); }
