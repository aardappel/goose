// A guard's else block must diverge (§6.4).
fn main() {
    let n = 1;
    guard n > 0 else { print(n); }
    print(n);
}
