// error: returning a reference rooted in loc, which dies with the call of apply that this return leaves
// A block's own locals live in the call of the function it is handed to,
// which its return leaves.
var G: i64 = 7;
fn apply<F>() -> i64 { F(1); return 0; }
fn pick() -> i64& {
    apply() { k => var loc: i64 = k + 41; return loc; };
    return G;
}
fn main() {
    print(pick() + 1);
}
