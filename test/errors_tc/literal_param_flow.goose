// A literal passed on through a second generic function is checked against
// the innermost use too (§7.7).
import std;
fn fill2(xs, v) { push_n(xs, v, 2); }
fn main() {
    var flags: u8[>..] = [];
    fill2(flags, -1);
    print(flags.len);
}
