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

// A call of a nested function leaves the variables around it as the body
// does, the calls that share its check too: the else branch's call finds n
// unassigned, as the then branch's did, and assigns it all the same, so n
// is assigned after the if; each pass of the loop after its first starts
// with n unassigned, as its first did, and the call assigns it there too
// (§4.4, §7.5).
struct Cell { key: i64 }
var cells: Cell[>..] = [Cell { key: 4 }];

fn branches(c: bool) {
    var n: i64;
    fn setn(k: i64) { n = k; }
    if c { setn(1); } else { setn(2); }
    print(n);
}

fn looped() {
    var n: i64;
    fn setn(k: i64) { n = k; }
    var v: Cell&? = null;
    for i in 3 {
        setn(10 + i);
        print(n);
        v .= &cells[0];
    }
    if v { print(v.key); }
}

fn main() {
    exits(true);
    exits(false);
    reused(true, true);
    reused(false, false);
    branches(true);
    branches(false);
    looped();
}
