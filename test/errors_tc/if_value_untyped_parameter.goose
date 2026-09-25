// error: a is not fixed-size and is not copied implicitly
// An untyped parameter takes the if's value as it is, a copy of the branch
// taken, as an un-annotated let would (§4.1, §7.7).
fn total(xs) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    t
}
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len > b.len;
    print(total(if c { a } else { b }));
}
