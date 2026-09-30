// error: cannot clear g while arr is still used
// A slice rendered as an element of a slice printed lies where that slice
// points (§3.7): a format hook taking it by reference re-points arr[0],
// which holds a view of g once print returns (§5.1).
var g: u8[>..] = [];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = g[1..]; }
fn main() {
    g.append("xyz");
    var local: u8[>..] = [];
    local.append("abc");
    var arr: u8[:][>..] = [];
    arr.push(local[..]);
    let ss = arr[..];
    print(ss);
    g.clear();
    print(arr[0][0]);
}
