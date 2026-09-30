// error: cannot clear arr2 while h is still used: a reference into it was stored there
// The second call of keep finds h as the first one's check did, but hands
// it a reference into arr2, which that check's calls did not store: keep's
// body is checked again for it (§5.1).
struct H { r: i64&? }
var K: i64 = 7;
fn main() {
    var arr: i64[>..] = [];
    arr.push(1);
    var arr2: i64[>..] = [];
    arr2.push(2);
    var h = H { r: K };
    fn keep(p: i64&) { h.r .= p; }
    h.r .= arr[0];
    keep(arr[0]);
    keep(arr2[0]);
    arr2.clear();
    arr2.push(99);
    if h.r { print(h.r); }
}
