struct Empty { count: i64 }
struct Holder { bytes: const u8[:], empty: Empty }
fn wrap(a: i64[>..]&) -> Holder {
    return Holder { a.bytes_of(), Empty { 0 } };
}
fn bad() -> const u8[:] {
    var a: i64[>..] = [42];
    let h = wrap(a);
    return h.bytes;
}
fn main() { print(bad()); }
