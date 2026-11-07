// error: cannot clear arr while keep is still used: a reference into it was stored there
// The back edge gives x what main pushed onto out before the call, a
// reference into arr, which the next activation keeps: the call records
// that keep may hold it, so clearing arr while keep is used is an error
// (§5.1, §7.8).
fn main() {
    var arr: i64[>..] = [1, 2, 3];
    var base: i64[>..] = [5];
    var out: (i64&)[>..<] = [];
    var keep: (i64&)[>..<] = [];
    recursive fn walk(n: i64, x: i64&) {
        keep.push(x);
        if n > 0 { for i in 0..out.len { walk(n - 1, out[i]); } }
    }
    out.push(arr[0]);
    walk(1, base[0]);
    arr.clear();
    print(keep[1]);
}
