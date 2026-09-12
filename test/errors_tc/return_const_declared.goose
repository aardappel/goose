fn readonly(xs: u8[:]) -> const u8[:] { return xs; }
fn main() {
    var a: u8[3] = "abc";
    let view = readonly(a[..]);
    view[0] = 'X';
}
