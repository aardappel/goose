// error: returning a reference rooted in local, which dies with this function
// A slice read out of a struct literal's field points where the field's
// initializer does, not at static data (§9.5).
struct Named { s: const i64[:] }
fn name() -> const i64[:] {
    let local = [1, 2, 3];
    Named { s: local[..] }.s
}
fn main() { print(name()[0]); }
