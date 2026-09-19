// Functions and function values written in a function value's body name its
// parameters and locals (§7.5, §7.6). Each check of a call of the value
// clones the body and binds them anew: another call site, or the same call
// checked again as an argument, specializes what the body declares again,
// which then captures the variables codegen declares.
enum M { A { n: i64 }, B }
fn apply<F>(n: i64) -> i64 { F(n) }
fn id(n: i64) -> i64 { n }
// F(n) is an argument, checked for resolution and again against id's
// parameter: twice, and in reapply2 four times.
fn reapply<F>(n: i64) -> i64 { id(F(n)) }
fn reapply2<F>(n: i64) -> i64 { id(id(F(n))) }
fn both<F>(a: i64, b: i64) -> i64 { F(a) * 100 + F(b) }
fn pass<G>(n: i64) -> i64 { apply(n, G()) }
fn with_text<F>(s: u8[:]) -> i64 { F(s) }
fn with_arr<F>(xs: i64[>..]&) -> i64 { F(xs) }
recursive fn countdown<F>(k: i64) {
    if k > 0 { countdown(k - 1, F); }
    F(k);
}
fn find() -> i64 {
    // A plain return in a function value in a function value leaves find.
    apply(4) { x => apply(3) { y => if x + y == 7 { return x * y; } 0 } };
    -1
}
let G1 = apply(4) { fn g() -> i64 { it * 3 } g() };
let G2 = apply(4) { x => apply(5) { y => fn g() -> i64 { x * y } g() } };
// A global initializer's local, named in its function values' bodies.
let G3 = block {
    let q = 3;
    break apply(4) { fn g() -> i64 { q * it } g() } + pass(5) { fn h(n: i64) -> i64 { n * q } h };
};
fn main() {
    let base = 100;
    let m: M = M.A { n: 5 };
    // A nested function naming a local, the parameter, the definer's local.
    print(apply(4) { let k = it + 1; fn g() -> i64 { base + k } g() });
    print(apply(4) { fn g() -> i64 { it * 2 } g() });
    print(apply(4) { fn g() -> i64 { base } g() + it });
    // Checked again as an argument, and at two call sites.
    print(reapply(4) { let k = it + 1; fn g() -> i64 { k * it } g() });
    print(reapply2(4) { let k = it + 2; fn g() -> i64 { k * it } g() });
    print(both(3, 4) { fn g() -> i64 { it * it } g() });
    // Named as a function value.
    print(apply(4) { let k = it * 10; fn g(n: i64) -> i64 { n + k } apply(1, g) });
    // Function values in function values, and nested functions in those.
    print(apply(4) { x => apply(3) { y => x * 10 + y } });
    print(apply(4) { let k = it + 1; apply(1) { y => k * 10 + y } });
    print(apply(4) { x => apply(3) { y => fn g() -> i64 { x * 10 + y } g() } });
    print(reapply(4) { x => reapply(3) { y => fn g() -> i64 { x * 10 + y } g() } });
    print(apply(2) { a => fn g(b: i64) -> i64 { apply(b) { c => a * 100 + b * 10 + c } } g(3) });
    print(both(1, 2) { a => both(a, a + 1) { b => fn g() -> i64 { a * 10 + b } g() } });
    // Writes, a var parameter, recursion (a tail call and not), a recursive caller.
    print(apply(4) { var acc = 0; fn add(n: i64) { acc += n; } add(it); add(it * 2); acc });
    print(apply(4) { var x => fn bump() { x += 1; } bump(); x });
    print(apply(5) {
        let step = 2;
        recursive fn down(n: i64) -> i64 { if n <= 0 { return 0; } return n + down(n - step); }
        down(it)
    });
    print(apply(6) {
        let step = 2;
        recursive fn sum(n: i64, t: i64) -> i64 { if n <= 0 { return t; } return sum(n - step, t + n); }
        sum(it, 0)
    });
    countdown(2) { fn g() -> i64 { it * 10 } print(g()); }
    // Arrays, text, a reference, a constant, a loop variable, a binder.
    print(apply(3) {
        var xs: i64[>..] = [];
        fn fill(n: i64) { for i in 0..n { xs.push(i * i); } }
        fill(it);
        xs.len * 100 + xs[2]
    });
    print(apply(2) { let xs: i64[4] = [5, 6, 7, 8]; fn at(i: i64) -> i64 { xs[i + it] } at(1) });
    print(with_text("hello") { fn g() -> i64 { it.len } g() });
    var arr: i64[>..] = [1, 2, 3];
    print(with_arr(arr) { fn g() -> i64 { it.push(10); it.len } g() }, " ", arr);
    print(reapply(4) { let k = 7; fn g() -> i64 { k * it } g() });
    print(reapply(4) { var t = 0; for i in 0..it { fn h() -> i64 { i * it } t += h(); } t });
    print(reapply(4) { match m { A a => { fn g() -> i64 { a.n + it } g() }, B => 0 } });
    // A nested function declared outside sees its own scope, not the body's.
    let x = 1;
    fn gx() -> i64 { x }
    fn gx2(n: i64) -> i64 { n + x }
    print(apply(4) { let x = 50; gx() + x });
    print(apply(4) { let x = 50; apply(5, gx2) + x });
    // Yielded out of the body, which has ended when it is called.
    print(pass(5) { fn h(n: i64) -> i64 { n + base } h });
    print(find(), " ", G1, " ", G2, " ", G3);
}
