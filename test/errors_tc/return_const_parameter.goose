// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
fn readonly(xs: const u8[:]) -> u8[:] { return xs; }
fn main() {
    var a: u8[3] = "abc";
    let view = readonly(a[..]);
    view[0] = 'X';
}
