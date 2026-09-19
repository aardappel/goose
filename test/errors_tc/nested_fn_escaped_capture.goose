// error: h names k
// error: whose scope has ended where h is called
// A nested function whose value leaves the block declaring it cannot name
// that block's k once the block has ended, and does not name the k of the
// scope it is called in instead.
fn apply<F>(n: i64) -> i64 { F(n) }
fn main() {
    let k = 100;
    print(apply(5, block { let k = 7; fn h(x: i64) -> i64 { x + k } break h; }));
}
