// error: storing a reference rooted at x, which does not outlive the destination
// A struct holding a reference stores what it holds (§9.2), in an array
// appended to a global as in one pushed.
struct S { r: i64&, k: i64 }
var g: S[>..] = [];
fn leak() {
    let x = 5;
    let ss: S[2] = [S { r: x, k: 1 }, S { r: x, k: 2 }];
    g.append(ss);
}
fn main() { leak(); print(g.len); }
