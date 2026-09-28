// error: call to area dispatches on a variant only with every argument written
// A dispatched call evaluates its arguments once, before the tag picks the
// case, so no case's defaults are passed (§8.2).
enum Shape { Circle { r: f64 }, Square { s: f64 } }
fn area(c: Shape.Circle, k: f64 = 1.0) -> f64 { c.r * c.r * 3.0 * k }
fn area(s: Shape.Square, k: f64 = 1.0) -> f64 { s.s * s.s * k }
fn main() {
    let sh: Shape = Shape.Circle { r: 1.0 };
    print(area(sh, 2.0));
    print(area(sh));
}
