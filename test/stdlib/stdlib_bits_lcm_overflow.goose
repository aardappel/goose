// runtime-debug: an lcm past its type's maximum fails an assert, an
// unsigned one included, rather than wrap.
import std;
fn main() {
    let a: u8 = 15;
    let b: u8 = 17;
    let c: u8 = 16;
    print(lcm(a, b));
    print(lcm(b, c));
}
