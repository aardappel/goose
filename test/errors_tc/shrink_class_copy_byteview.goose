// error: cannot call f: it shrinks arr while h is still used, and h may refer into arr
// The caller's holder hs views holds a byte view of arr's elements (§5.1).
struct H { s: const u8[:] }
fn f(arr: i32[>..]&, hs: H[:]) {
    let h = hs[0];
    arr.clear();
    arr.push(0x5A5A5A5A);
    arr.push(0x5A5A5A5A);
    arr.push(0x5A5A5A5A);
    print(h.s[0]);
}
fn main() {
    var arr: i32[>..] = [1, 2, 3];
    var hs: H[>..] = [];
    hs.push(H { s: bytes_of(arr[..]) });
    f(arr, hs[..]);
}
