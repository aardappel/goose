// error: h names k
// error: whose scope has ended where h is called
// A nested function called inside its block and again after the block's
// value took it out: the call after the block cannot reuse the
// specialization the first call checked, which names the block's k.
fn apply<F>(n: i64) -> i64 { F(n) }
fn main() {
    print(apply(5, block { let k = 7; fn h(x: i64) -> i64 { x + k } let y = h(1); break h; }));
}
