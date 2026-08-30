// No implicit int/flt mixing in expressions (§6.3).
fn main() {
    let x = 1 + 2.0;
    print(x);
}
