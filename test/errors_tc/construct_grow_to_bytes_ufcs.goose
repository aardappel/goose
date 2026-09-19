// error: cannot call wrap, which grows a: the run appended to a is still under construction
// A callee still being checked grows what its text grows: for image, the
// output of xs.to_bytes(a), which is to_bytes(xs, a) (§7.1). Its image
// would land inside the run add appends to a, which wrap's result is built
// in place as (§1.3(4)).
recursive fn image(a: u8[>..]&, n: i64) -> u8 {
    let xs: i64[2] = [1, 2];
    xs.to_bytes(a);
    if n > 0 { add(a, n - 1); }
    return 9;
}
recursive fn wrap(a: u8[>..]&, n: i64) -> u8[>..] {
    return [7, image(a, n), 8];
}
recursive fn add(a: u8[>..]&, n: i64) {
    a.append(wrap(a, n));
}
fn main() {
    var a: u8[>..] = [];
    image(a, 1);
    print(a.len);
}
