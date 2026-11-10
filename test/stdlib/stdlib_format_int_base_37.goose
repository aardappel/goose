// runtime-debug: format_int rejects a base outside 2..36.
import std;
fn main() {
    var out: u8[>..] = [];
    format_int(out, 255, 37, 0, ' ');
    print("unreachable");
}
