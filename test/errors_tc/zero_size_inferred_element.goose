// error: an element cannot be a variant type with no fields: Mode.Off
// Where nothing names an array literal's type, its elements' type decides
// it, and that is no element either (§3.4).
enum Mode { Off, Level { v: i64 } }

fn main() {
    for m in [Mode.Off {}, Mode.Off {}] { print(m); }
}
