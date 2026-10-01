// error: an element cannot be a variant type with no fields: Mode.Off
// Nor is a variant type with no fields an array's element (§3.4), here in a
// specialization whose type parameter makes it one.
enum Mode { Off, Level { v: i64 } }

fn three<T>(x: T) -> i64 {
    let a: T[3] = [x, x, x];
    a.len
}

fn main() {
    print(three(Mode.Off {}));
}
