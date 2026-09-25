// error: returning a reference into a temporary
// A `for &x` over a match's value binds elements of its temporary copy
// (§9.2).
var g: i64[3] = [1, 2, 3];
fn first(k: i64) -> i64& {
    for &x in (match k { 0 => g, _ => g }) { return x; }
    abort("empty")
}
fn main() { print(first(0)); }
