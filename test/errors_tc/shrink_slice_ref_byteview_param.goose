// error: cannot clear xs while p
// error: is still used: the slice it refers to may point into xs
// r names s, whose slice is a byte view of xs: the u8 slice p refers to may
// view xs's i64 elements, which the clear frees (§5.2, serialization).
fn peek(p: const u8[:]&, xs: i64[>..<]&) {
    xs.clear();
    xs.push(7);
    print(p[0]);
}
fn main() {
    var xs: i64[>..<] = [1, 2];
    var s = xs.bytes_of();
    let r = &s;
    peek(r, xs);
}
