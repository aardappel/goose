// error: a is not fixed-size and is not copied implicitly
// A for loop that is a function's final statement gives no value: the if's
// value it iterates is a copy of `a`, which the rest of the loop could
// still change, so it takes copy() (§4.1).
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len > b.len;
    for e in if c { a } else { b } { a.push(e); }
}
