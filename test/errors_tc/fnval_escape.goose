// Function values cannot escape into data (§7.6).
fn h(v: i64) { print(v) }
fn main() {
    let g = h;
    g(1);
}
