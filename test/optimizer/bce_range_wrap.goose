// A range endpoint written `x + 1` is that sum only where the addition
// cannot wrap: just below i64's top, `x..x + 1` wraps to an empty range in a
// release build, so a counted push loop over it states no length.
import std;
fn main() {
    var vals: i64[>..] = [9223372036854775807, 5];
    let x = vals[0];
    var a: i64[>..] = [];
    for j in x..x + 1 { a.push(j); }
    print(a.len);
    print(a[0]); // bce:keep
}
