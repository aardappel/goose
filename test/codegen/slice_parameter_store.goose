struct Holder { p: i64? }
fn save(xs: Holder[:], p: i64&) { xs[0].p .= p; }
fn swap(xs: Holder[:]) { let t = xs[0]; xs[0] = xs[1]; xs[1] = t; }
fn main() {
    var data: i64[>..] = [42, 99];
    var holders: Holder[2] = [Holder {}, Holder {}];
    save(holders, data[0]);
    save(holders[1..], data[1]);
    swap(holders);
    if holders[0].p { print(holders[0].p); }
    if holders[1].p { print(holders[1].p); }
}
