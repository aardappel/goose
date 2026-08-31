// A function with declared returns cannot fall off the end.
fn f(n: i64) -> i64 {
    if n > 0 { return 1; }
}
fn main() {
    print(f(1));
}
