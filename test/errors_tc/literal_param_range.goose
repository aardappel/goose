// error: argument 2 of push_n: constant 300 does not fit u8, which parameter v takes
// A literal argument must fit every type the parameter adapts to inside the
// callee, here u8 through push_n's push (§7.7).
import std;
fn main() {
    var flags: u8[>..] = [];
    push_n(flags, 300, 2);
    print(flags.len);
}
