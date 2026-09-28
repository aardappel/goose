// A `<` before a qualified name is a comparison unless a type argument list
// follows (docs/design/namespaces.md). The parser tries a type argument list
// first; what it took for types there is no type when none follows, so an
// unknown qualified type name among them is no error.
struct lim::Pair<T> { a: T, b: T }
let lim::COUNT = 10;
var lim::sizes: i64[>..] = [3, 5, 8];
let ::LIMIT = 4;

fn lim::pick<T>(p: lim::Pair<T>, first: bool) -> T { if first { p.a } else { p.b } }

// Shares its name with the type parameter below, which takes priority over
// it throughout that function's declaration (§11.1), comparisons in its body
// or not.
struct Item { v: i64 }

// The least and the greatest of the first lim::COUNT of xs.
fn lim::bounds<Item>(xs: Item[:]) -> lim::Pair<Item> {
    var lo = xs[0];
    var hi = xs[0];
    for x, i in xs {
        if i < lim::COUNT {
            if x < lo { lo = x; }
            if hi < x { hi = x; }
        }
    }
    lim::Pair<Item> { lo, hi }
}

fn main() {
    let a = 3;
    if a < lim::COUNT { print("a < lim::COUNT"); }
    if a<lim::COUNT { print("a<lim::COUNT"); }
    if a < lim::sizes.len { print("never"); } else { print("a == lim::sizes.len"); }
    if a < lim::sizes[1] { print("a < lim::sizes[1]"); }
    if lim::sizes[0] < lim::COUNT && a < ::LIMIT { print("both"); }
    let lt = a < lim::COUNT;
    print(lt, " ", a < ::LIMIT, " ", lim::COUNT < a);
    // Type argument lists that do follow: a qualified generic function's
    // and a qualified generic struct's.
    let p = lim::Pair<i64> { a, lim::COUNT };
    print(lim::pick<i64>(p, true), " ", lim::pick<i64>(p, false));
    let xs: i64[12] = [5, 7, 3, 9, 4, 6, 8, 5, 7, 6, 1, 99];
    let b = lim::bounds<i64>(xs[..]);
    print(b.a, " ", b.b);
}
