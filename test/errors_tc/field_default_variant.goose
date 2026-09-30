// error: the default of field k of Shape.Circle leads to this construction of Shape.Circle, which takes it again
// A variant literal takes its payload's defaults as a struct literal does
// (§3.2, §4.2); passing it to a function checks it where it is written.
enum Shape { Circle { r: i64 = 1, k: i64 = size(Shape.Circle { .. }) }, Square { s: i64 } }
fn size(s: Shape) -> i64 {
    match s { Circle x => x.r, Square x => x.s }
}
fn main() {
    let s: Shape = Shape.Circle { .. };
    print(size(s));
}
