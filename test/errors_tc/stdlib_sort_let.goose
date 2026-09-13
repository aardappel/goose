// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
// sort writes through its slice, so a const array is not sortable (§9.5).
import std;
fn main() {
    const xs = [3, 1, 2];
    sort(xs);
    print(xs[0]);
}
