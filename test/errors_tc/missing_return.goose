// A function with declared returns cannot fall off the end.
fn f(n: int) -> int {
    if n > 0 { return 1; }
}
fn main() {
    print(f(1));
}
