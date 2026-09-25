// error: its storage does not outlive this function: return copy(a)
// A reference to a non-fixed value is returned as that reference where the
// result type is inferred (§4.1, §7.1): `a` names the function's own local,
// which dies with it, and is not copied implicitly.
fn build() {
    var t: i64[>..] = [1];
    let a = t;
    a.push(2);
    return a;
}
fn main() { print(build()); }
