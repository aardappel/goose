// error: a is not fixed-size and is not copied implicitly
// A member builtin called on an if's value works on that value, a copy of
// the branch taken (§4.1): the push would land in the copy.
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len > b.len;
    (if c { a } else { b }).push(6);
    print(a.len);
}
