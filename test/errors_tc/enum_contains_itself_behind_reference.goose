// error: enum F contains itself by value
// F is reached only through E's reference to it, and still may not hold
// itself (§3.4).
enum E { A { f: F? }, B }
enum F { C { f: F }, D }
fn main() {
    let e = E.B;
    print(e);
}
