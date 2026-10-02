// error: constant 300 does not fit u8
// §6.1: a constant shifted by a count that is no constant adapts only to a
// type the constant fits.

fn main() {
    var k = 2;
    k += 1;
    let s: u8 = 300 << k;
    print(s);
}
