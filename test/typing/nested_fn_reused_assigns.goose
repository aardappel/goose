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
    branches(true);
    branches(false);
    looped();
}
