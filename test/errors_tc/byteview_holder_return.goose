struct Holder { bytes: const u8[:] }
fn wrap(a: i64[>..]&) -> Holder { return Holder { a.bytes_of() }; }
fn bad() -> const u8[:] {
    var a: i64[>..] = [42];
    let b = wrap(a);
    return b.bytes;
}
fn main() { print(bad()); }
