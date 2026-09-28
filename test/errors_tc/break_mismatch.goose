// error: expected a value of type i64, got f64
// Later breaks of one construct agree with the first (§6.4): a float does not
// become the integer an earlier break gave, as an integer literal there would
// have adapted to the float.
fn main() {
    var n: i64 = 1;
    let x = loop {
        let c = true;
        if c { break n; }
        break 2.5;
    };
    print(x);
}
