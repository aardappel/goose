// error: g (declared at
// error: is called before its declaration is reached
// A nested function may call one declared further on in its block, but only
// once that declaration is reached.
fn g() -> i64 { 1 }
fn main() {
    fn h() -> i64 { g() }
    print(h());
    fn g() -> i64 { 2 }
}
