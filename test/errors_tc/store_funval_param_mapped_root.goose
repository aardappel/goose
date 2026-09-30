// error: storing a reference rooted at arr, which does not outlive the destination
// h holds a reference into arr, which each's call passed for its parameter
// xs, whatever the block's check saw: G, a global, cannot hold it (§9.2).
struct H { r: i64&? }
fn each<F>(xs: i64[:]) { F(&xs[0]); }
var G = H { r: null };
fn f() {
    var arr: i64[>..] = [];
    arr.push(1);
    var h = H { r: null };
    each(arr[..]) { p => h.r .= p; };
    G = h;
}
fn main() { f(); }
