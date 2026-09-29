// error: re-binding gp to a reference that is not a pool reference, where it was bound to a reusable pool
// A global bound to a pool is a pool reference in every function (§5.4), so
// no function rebinds it to an array that is not one.
reusable var pool: i64[>..] = [1, 2];
var other: i64[>..] = [5, 6, 7];
var gp: i64[>..]& = pool;
fn b() { gp .= other; }
fn main() {
    b();
    print(gp.len);
}
