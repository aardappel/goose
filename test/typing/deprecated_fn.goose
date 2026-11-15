// warning: copy_into is deprecated: use copy_from
// warning: copy_into is deprecated: use copy_from
// warning: old is deprecated: use new
// warning: old is deprecated: use new
// warning: gone is deprecated: use kept
// warning: bare is deprecated
// A direct call warns, and so does naming the function as a value. The call
// through that value does not warn again. `deprecated` stays a name (spec §7.15).
import std;

deprecated "use new" fn old(x: i64) -> i64 { x + 1 }
fn new(x: i64) -> i64 { x + 1 }
fn apply<F>(x: i64) -> i64 { F(x) }
deprecated fn bare() -> i64 { 1 }

fn nest() -> i64 {
    deprecated "use kept" fn gone() -> i64 { 7 }
    fn kept() -> i64 { 7 }
    return gone();
}

fn main() {
    var d = [0, 0];
    copy_into(d, [1, 2]);
    d.copy_into([3, 4]);
    print(d[0], " ", d[1]);
    print(old(1));
    print(apply(2, old));
    print(nest());
    print(bare());
    let deprecated = 9;
    print(deprecated);
}
