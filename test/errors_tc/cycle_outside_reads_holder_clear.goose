// error: cannot clear arr while keep is still used
// The back edge gives v what main pushed onto out before the call, a
// holder referring into arr, which the next activation keeps: the call
// records that keep may hold what v refers to, so clearing arr while keep
// is used is an error (§5.1, §7.8).
enum Val { Ref { x: i64& }, Num { v: i64 } }
fn main() {
    var arr: i64[>..] = [1, 2, 3];
    var base: i64[>..] = [5];
    var out: Val[>..<] = [];
    var keep: Val[>..<] = [];
    recursive fn walk(n: i64, v: Val) {
        keep.push(v);
        if n > 0 { for i in 0..out.len { walk(n - 1, out[i]); } }
    }
    out.push(Val.Ref { x: arr[0] });
    walk(1, Val.Ref { x: base[0] });
    arr.clear();
    match keep[1] { Ref r => { print(r.x); }, _ => {} }
}
