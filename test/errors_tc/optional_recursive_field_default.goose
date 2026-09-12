// A recursive call may rebind what a `.=` names in a function that a field
// default calls, which a struct literal runs without naming it: the read after
// the back edge cannot rely on g, even though the instance already exists.
var n = 41;
var g: i64? = null;
fn clear() -> i64 { g .= null; return 1; }
struct S<T> { y: T, x: i64 = clear() }
recursive fn r(k: i64) {
    if k > 0 { r(k - 1); }
    print(g + 1);
    let s = S<i64> { y: k };
    print(s.x);
}
fn main() {
    let a = S<i64> { y: 0 };
    print(a.x);
    g .= n;
    r(1);
}
