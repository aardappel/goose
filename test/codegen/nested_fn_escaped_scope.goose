// A nested function whose value leaves the block or function value body
// declaring it is called after that scope ended (§7.5): it names what is
// still in scope, calls the nested functions declared beside it, recursion
// included, and is specialized apart from its calls inside the scope.
fn apply<F>(n: i64) -> i64 { F(n) }
fn pass<G>(n: i64) -> i64 { apply(n, G()) }
fn main() {
    let base = 1000;
    print(apply(5, block { let unused = 3; fn e(x: i64) -> i64 { x + d(x) } fn d(x: i64) -> i64 { x * base } break e; }));
    print(apply(5, block { let k = 7; fn e(x: i64) -> i64 { x + base } print(e(k)); break e; }));
    print(apply(5, block {
        let u = 1;
        recursive fn e(x: i64) -> i64 { if x > 0 { return base + e(x - 1); } return 0; }
        break e;
    }));
    print(pass(5) { let m = 2; fn e(n: i64) -> i64 { n * base + d() } fn d() -> i64 { base / 10 } e });
    print(apply(2, loop { let k = 1; fn e(x: i64) -> i64 { x * 3 + base } break e; }));
    // Out of a block in a nested function.
    fn deep() -> i64 {
        let q = 4;
        apply(5, block { let u = 1; fn e(x: i64) -> i64 { x * q + d(x) } fn d(x: i64) -> i64 { x + base } break e; })
    }
    print(deep());
}
