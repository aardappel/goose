// error: h names k
// error: whose scope has ended where h is called
// Nor can it write that k, or write the k of the scope it is called in
// instead.
fn apply<F>(n: i64) -> i64 { F(n) }
fn main() {
    var k = 100;
    print(apply(5, block { var k = 7; fn h(x: i64) -> i64 { k += x; x } break h; }), " ", k);
}
