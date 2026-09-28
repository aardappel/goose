// error: match pattern 9223372036854775808 never matches
// A u64 match orders its patterns unsigned (§8.1), so a range from i64's
// largest value upward holds the value past it.
fn main() {
    var x: u64 = 4;
    print(match x { 9223372036854775807..9223372036854775810 => 1,
                    9223372036854775808 => 2, _ => 0 });
}
