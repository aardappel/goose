// Function values cannot escape into data (§7.6).
fn h(v: int) { print(v) }
fn main() {
    let g = h;
    g(1);
}
