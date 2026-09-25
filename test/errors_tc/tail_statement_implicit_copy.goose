// error: a is not fixed-size and is not copied implicitly
// A function's final expression moves the function's own locals as a return
// does (§4.1), but a statement inside it is no part of that value: `a`
// outlives it, and is copied there only by copy().
fn grown(c: bool) -> i64 {
    var a: i64[>..] = [1];
    if c { var b: i64[>..] = a; b.push(2); b.len } else { a.len }
}
fn main() { print(grown(true)); }
