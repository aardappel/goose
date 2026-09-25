// error: a is not fixed-size and is not copied implicitly
// A view of an if's value views a copy of the branch taken, however briefly
// (§4.1); `if c { a[..] } else { b[..] }` views the arrays themselves.
fn total(xs: const i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    t
}
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len > b.len;
    print(total((if c { a } else { b })[..]));
}
