// error: the index of a range or count counts its iterations as an i64
// A range loop's index binder counts iterations and takes no type of its own
// (§6.5); the range's binder does.
fn main() {
    for i, k: u8 in 0..10 {
        print(i, k);
    }
}
