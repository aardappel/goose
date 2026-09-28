// error: branches have mismatched types: i64[3] vs i64[2]
// A first break whose value joins arrays of two types gives the block no type
// for later breaks to agree with: with no slice among them, and no
// destination type, they do not join as one (§6.4).
fn main() {
    let a: i64[3] = [1, 2, 3];
    let b: i64[2] = [4, 5];
    let e: i64[4] = [6, 7, 8, 9];
    let c = a[0] > 0;
    let d = b[0] > 0;
    let s = block {
        if c { break if d { a } else { b }; }
        break e;
    };
    print(s);
}
