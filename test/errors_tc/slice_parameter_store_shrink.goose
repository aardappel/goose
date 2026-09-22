// error: cannot clear
struct Holder { p: i64? }
fn save(xs: Holder[:], p: i64&) { xs[0].p .= p; }
fn main() {
    var data: i64[>..] = [42];
    var holders: Holder[1] = [Holder {}];
    save(holders, data[0]);
    data.clear();
    if holders[0].p { print(holders[0].p); }
}
