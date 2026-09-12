struct Holder { bytes: const u8[:] }
fn bytes(a: i64[>..]&) -> const u8[:] {
    let h = Holder { a.bytes_of() };
    return h.bytes;
}
fn main() {
    var a: i64[>..] = [42];
    let b = bytes(a);
    print(b.len);
    a.clear();
    a.push(7);
    let h = Holder { a.bytes_of() };
    let c = h;
    print(c.bytes.len);
    a.clear();
    var s: i64[>..<] = [1, 2];
    let v = if a.len == 0 { s.bytes_of() } else { s.bytes_of() };
    print(v.len);
    s.clear();
    print(a.len, " ", s.len);
}
