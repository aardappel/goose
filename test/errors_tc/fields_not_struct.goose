// error: fields takes a struct, not i64[4]
// fields walks a struct's fields (§12); an array is walked by the program.
fn main() {
    let xs = [1, 2, 3, 4];
    fields(xs) { name, f => print(name) }
}
