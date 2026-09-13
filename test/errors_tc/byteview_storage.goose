// error: storing a reference into a, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
struct Holder { bytes: const u8[:] }
fn main() {
 var a: i64[>..<] = [42];
 let b = Holder { a.bytes_of() };
 a.clear();
 print(b.bytes);
}
