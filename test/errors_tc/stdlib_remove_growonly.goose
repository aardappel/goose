// remove_at needs pop, which a grow-only array has not got (§5.1).
import std;
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    print(remove_at(xs, 1));
}
