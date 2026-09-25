// error: a is not fixed-size and is not copied implicitly
// A builtin called on an if's value takes that value, a copy of the branch
// taken (§4.1).
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len > b.len;
    (if c { a } else { b }).print();
}
