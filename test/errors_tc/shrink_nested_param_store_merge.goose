// error: cannot clear arr while g is still used: a reference into it was stored there
// keep stores its parameter into h, which the call makes a reference into
// arr: h holds what the call passed, not the class keep's body saw, and so
// does a merge of it (§5.1).
struct H { r: i64&? }
var K: i64 = 7;
fn main() {
    var arr: i64[>..] = [];
    arr.push(1);
    var h = H { r: K };
    var h2 = H { r: K };
    fn keep(p: i64&) { h.r .= p; }
    keep(arr[0]);
    let g = if arr.len > 0 { h } else { h2 };
    arr.clear();
    arr.push(99);
    if g.r { print(g.r); }
}
