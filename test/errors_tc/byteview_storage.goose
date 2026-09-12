struct Holder { bytes: const u8[:] }
fn main() {
 var a: i64[>..<] = [42];
 let b = Holder { a.bytes_of() };
 a.clear();
 print(b.bytes);
}
