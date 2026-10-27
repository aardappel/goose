// error: an element cannot be a variant type with no fields: Mode.Off
// error: in fn main()
// count's parameter gives the empty array literal argument its element type:
// the literal, an array of Mode.Off, is rejected at the call in main's body
// (the second marker), before count's own parameter type is (§3.4).
enum Mode { Off, Level { v: i64 } }

fn count(offs: Mode.Off[:]) -> i64 { offs.len }

fn main() {
    print(count([]));
}
