fn bytes(a: i64[>..<]&) -> const u8[:] { return a.bytes_of(); }
fn main() {
 var a: i64[>..<] = [42];
 let b = bytes(a);
 a.clear();
 print(b);
}
