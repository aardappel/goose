// error: k (declared at
// error: is called before its declaration is reached
// A function declared in a global initializer may call one declared further
// on in its block, but only once that declaration is reached (§7.5).
let g = block {
    fn h() -> i64 { k() }
    let a = h();
    fn k() -> i64 { 2 }
    a
};
fn main() { print(g); }
