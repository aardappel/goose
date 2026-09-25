// error: a is not fixed-size and is not copied implicitly
// A loop's value is a copy of what its break carries (§4.1).
fn main() {
    var a: i64[>..] = [1, 2, 3];
    let x = loop { break a; };
    print(x.len);
}
