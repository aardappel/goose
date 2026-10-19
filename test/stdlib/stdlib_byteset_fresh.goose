// A ByteSet searched right after byte_set builds it into a local: the
// runtime reads the set as a C struct of its own, which gcc -O2 once took
// for another object than the stores that built it, and searched the set
// before them.
import std;

fn main() {
    let comma = byte_set(",");
    print(find_any("hello, world", comma), " ", find_any("abc,", comma));    // 5 3
    let digits = byte_set("0123456789");
    print(find_pair("ab12cd", digits, 1, digits));                            // 2
}
