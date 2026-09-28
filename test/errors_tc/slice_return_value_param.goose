// error: returning a reference rooted in a, which dies with this function
// A by-value array parameter is the function's own copy (§3.10, §9.2).
fn f(a: u8[4]) -> const u8[:] { a }
fn main() {
    let x: u8[4] = [65, 66, 67, 68];
    print(f(x));
}
