// error: optional value must be narrowed (if/guard/assert), not matched
// An optional is not transparent (§3.8): an integer match through an i64?
// needs it narrowed first, as its other uses do.
fn main() {
    var a = 3;
    var o: i64? = a;
    print(match o { 3 => 1, _ => 0 });
}
