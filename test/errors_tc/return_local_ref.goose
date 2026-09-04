// A returned reference must be rooted visibly to the caller (§9.2).
fn f() -> i64& {
    var x = 1;
    return x;
}
fn main() {
    print(f());
}
