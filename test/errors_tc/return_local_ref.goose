// A returned reference must be rooted visibly to the caller (§9.2).
fn f() -> int& {
    var x = 1;
    return &x;
}
fn main() {
    print(f());
}
