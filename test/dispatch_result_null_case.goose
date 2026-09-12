// A case that only returns null yields no reference, so the dispatched result
// is as writable as the reference the other case returns.
enum Shape { Circle { r: i64 }, Point { k: i64 } }
fn radius(s: Shape.Circle&) -> i64&? { return s.r; }
fn radius(s: Shape.Point&) -> i64&? { return null; }
fn widen(s: Shape..&) {
    let r: i64&? = radius(s);
    guard r else { print("no radius"); return; }
    r = 5;
    print(r);
}
fn main() {
    var c: Shape.. = Shape.Circle { r: 3 };
    widen(c);
    var p: Shape.. = Shape.Point { k: 4 };
    widen(p);
}
