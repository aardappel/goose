// error: cannot assign to let x, which may be assigned already
// each calls the block once per iteration of its loop, and the second finds
// x assigned by the first (§4.4, §7.6).
fn each<F>(k: i64) { for i in k { F(i); } }
fn main() {
    let x: i64;
    each(2) { x = 5; };
}
