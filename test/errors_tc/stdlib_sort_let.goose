// sort writes through its slice, so a const array is not sortable (§9.5).
import std;
fn main() {
    const xs = [3, 1, 2];
    sort(xs);
    print(xs[0]);
}
