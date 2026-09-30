// error: cannot call clr, which shrinks a: ss[..], evaluated earlier in the statement, may still refer into a
// show is handed ss[..] before clr runs, and reads the elements' views of
// a after it (§5.1).
fn clr(a: u8[>..]&) -> i64 {
    a.clear();
    a.append("ZZZZZZZZ");
    return 0;
}
fn show(v: (u8[:])[:], n: i64) {
    print(v[0]);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    show(ss[..], clr(a));
}
