// error: an element cannot be a variant type with no fields: Mode.Off
// A slice's elements are an array's, so a slice of a variant type with no
// fields is rejected too (§3.4).
enum Mode { Off, Level { v: i64 } }

fn count(offs: Mode.Off[:]) -> i64 { offs.len }

fn main() {
    print(count([]));
}
