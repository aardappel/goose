// error: a is not fixed-size and is not copied implicitly
// error: pass copy(a) for a copy, or bind it by reference
// An if's value is a copy of what the branch taken names, and nothing here
// binds it by reference: a non-fixed branch naming storage takes copy()
// (§4.1).
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len > 1;
    let x = if c { a } else { b };
    a[0] = 5;
    print(x[0]);
}
