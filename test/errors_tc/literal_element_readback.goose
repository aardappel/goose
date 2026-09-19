// error: returning a reference rooted in local, which dies with this function
// A slice read out of an array literal points where the literal's elements
// point, not at static data (§9.5).
fn first() -> const i64[:] {
    let local = [1, 2, 3];
    [local[..]][0]
}
fn main() { print(first()[0]); }
