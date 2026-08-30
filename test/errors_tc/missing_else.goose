// An if used as a value requires both arms (§6.4).
fn main() {
    let c = true;
    let x = if c { 1 };
    print(x);
}
