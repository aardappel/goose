// error: returning a reference rooted in loc, which dies with this function
// What the function calling the block hands it is rooted where that call's
// argument is: here a local of the function the block returns from.
var G: i64 = 7;
fn each_elem<F>(xs: i64[:]) { for i in xs.len { F(&xs[i]); } }
fn outer() -> i64& {
    var loc: i64[3] = [1, 2, 3];
    each_elem(loc) { x: i64& => return x; };
    return G;
}
fn main() {
    print(outer() + 1);
}
