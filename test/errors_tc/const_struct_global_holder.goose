// error: storing a reference rooted at y, which does not outlive the destination
// A global var of a const holder type outlives every function's locals
// (§9.2), as one of a plain holder type does.
struct H { r: i64& }
var gx = 1;
var G: const H = H { r: gx };
fn rebind() {
    var y = 2;
    G = H { r: y };
}
fn main() {
    rebind();
    print(G.r);
}
