// sort writes through its slice, so a let array is not sortable (§9.5).
import std;
fn main() {
    let xs = [3, 1, 2];
    sort(xs);
    print(xs[0]);
}
