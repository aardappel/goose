// A struct holding a reference to a local, copied into a global container:
// the copy would outlive the local (§9.2).
struct S { r: i64&, k: i64 }
var g: S[>..] = [];
fn leak() {
    var x = 5;
    let s = S { r: x, k: 1 };
    g.push(s);
}
fn main() { leak(); print(g.len); }
