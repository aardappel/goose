// error: cannot clear arr while g is still used: a reference into it was stored there
// The block stores what each hands it, a reference into each's parameter,
// into h, which each's call makes a reference into arr: h holds what the
// call passed, and so does a merge of it (§5.1).
struct H { r: i64&? }
fn each<F>(xs: i64[:]) { F(&xs[0]); }
var K: i64 = 7;
fn main() {
    var arr: i64[>..] = [];
    arr.push(1);
    var h = H { r: K };
    var h2 = H { r: K };
    each(arr[..]) { p => h.r .= p; };
    let g = if arr.len > 0 { h } else { h2 };
    arr.clear();
    arr.push(99);
    if g.r { print(g.r); }
}
