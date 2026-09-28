// error: empty range in match pattern
// A u64 match compares its patterns unsigned (§8.1), so a range from the
// type's largest value down to a small one is empty.
fn main() {
    var x: u64 = 3;
    print(match x { 0..3 => 1, 18446744073709551615..5 => 2, _ => 0 });
}
