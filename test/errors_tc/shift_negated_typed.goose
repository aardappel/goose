// error: expected a value of type u64, got i64 (narrowing and sign changes require an explicit `as`)
// §6.1: unary minus gives a shifted constant a type of its own.

fn main() {
    var k = 2;
    k += 1;
    let m: u64 = -(1 << k);
    print(m);
}
