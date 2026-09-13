// A named variable-size local is built at the destination it is returned in.
fn one() -> u8[] { let s: u8[] = "fgh"; return s; }
fn two_diff() -> u8[], u8[] { let s: u8[] = "fgh"; let t: u8[] = "ij"; return s, t; }
fn two_first() -> u8[], i64 { let s: u8[] = "fgh"; return s, 5; }
fn two_same() -> u8[], u8[] { let s: u8[] = "fgh"; return s, s; }
fn main() {
    print(one());
    let a, b = two_diff();
    print(a, " ", b);
    let c, n = two_first();
    print(c, " ", n);
    let d, e = two_same();
    print(d, " ", e);
}
