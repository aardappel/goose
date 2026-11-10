// error: const assert condition x names a local variable, not a constant
// The condition is one a const if takes: a constant of the specialization,
// never a value computed as the program runs (§6.5).
fn main() {
    let x = 3;
    const assert x == 3, "x is three";
}
