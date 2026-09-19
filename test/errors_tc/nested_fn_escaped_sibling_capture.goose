// error: g names k
// error: whose scope has ended where g is called
// A nested function that leaves its block names nothing the block declared,
// but calls g, declared further on in the block, which names the block's k:
// neither may be called once the block has ended, whatever was checked
// inside it.
fn apply<F>(n: i64) -> i64 { F(n) }
fn main() {
    print(apply(5, block {
        fn h(x: i64) -> i64 { x + g() }
        let k = 7;
        fn g() -> i64 { k }
        let y = h(1);
        break h;
    }));
}
