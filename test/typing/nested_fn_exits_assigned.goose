// A call of a nested function leaves a variable around it assigned where
// every exit of the body does: each return, one that a function value's body
// or a callee's `return from` takes, and the end (§4.4, §7.5). A call reusing
// an earlier call's check takes the same exits, having assigned what the
// reused body had by each: the else branch's bail assigns n before it leaves
// setn, as the then branch's did.
fn each<F>(k: i64) { for i in k { F(i); } }

fn exits(c: bool) {
    var n: i64;
    fn setn(d: bool) {
        if d { n = 1; return; }
        each(2) { if !d { n = 2; return; } }
        n = 3;
    }
    setn(c);
    print(n);
}

fn reused(c: bool, x: bool) {
    var n: i64;
    fn setn(d: bool) {
        fn bail(e: bool) { n = 1; if e { return from setn; } }
        if x { bail(d); } else { bail(d); }
    }
    setn(c);
    print(n);
}

fn main() {
    exits(true);
    exits(false);
    reused(true, true);
    reused(false, false);
}
