// error: cannot clear data while holders is still used: a reference into it was stored there
// The store through the slice parameter's element lands in holders, which
// the clear then finds (§5.1).
struct Holder { p: i64? }
fn save(xs: Holder[:], p: i64&) { xs[0].p .= p; }
fn main() {
    var data: i64[>..] = [42];
    var holders: Holder[1] = [Holder {}];
    save(holders, data[0]);
    data.clear();
    if holders[0].p { print(holders[0].p); }
}
