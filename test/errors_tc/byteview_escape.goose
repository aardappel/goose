struct Holder { bytes: const u8[:] }
fn bytes() -> const u8[:] {
 var a: i64[>..] = [42];
 let b = Holder { a.bytes_of() };
 return b.bytes;
}
fn main() { print(bytes()); }
