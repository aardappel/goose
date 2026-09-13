// A case that never returns yields no reference, whether its variant comes
// first or last, so the dispatched result is as writable as the other case's.
enum Shape { Circle { r: i64 }, Point { k: i64 } }
fn radius(s: Shape.Circle&) -> i64& { return s.r; }
fn radius(s: Shape.Point&) -> i64& { abort("a point has no radius"); }
enum Flipped { Point { k: i64 }, Circle { r: i64 } }
fn flipped(s: Flipped.Circle&) -> i64& { return s.r; }
fn flipped(s: Flipped.Point&) -> i64& { abort("a point has no radius"); }
fn main() {
    var s: Shape.. = Shape.Circle { r: 3 };
    let r .= radius(s);
    r = 5;
    print(r);
    var f: Flipped.. = Flipped.Circle { r: 4 };
    let q .= flipped(f);
    q = 6;
    print(q);
}
