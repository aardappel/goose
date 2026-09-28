// error: branches have mismatched types: i64[3] vs i64[2]
// The first arms that do not agree are the ones reported, whatever a later
// arm is (§6.4).
fn main() {
    let a: i64[3] = [1, 2, 3];
    let b: i64[2] = [4, 5];
    for k in 3 {
        let s = match k { 0 => a, 1 => b, _ => 7 };
        print(s);
    }
}
