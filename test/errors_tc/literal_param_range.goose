// A literal argument must fit every type the parameter adapts to inside the
// callee, here u8 through push_n's push (§7.7).
import std;
fn main() {
    var flags: u8[>..] = [];
    push_n(flags, 300, 2);
    print(flags.len);
}
