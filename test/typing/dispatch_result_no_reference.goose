// A case that never returns yields no reference, whether its variant comes
// first or last, so the dispatched result is as writable as the other case's.
enum Shape { Circle { r: i64 }, Point { k: i64 } }
fn radius(s: Shape.Circle&) -> i64& { return s.r; }
fn radius(s: Shape.Point&) -> i64& { abort("a point has no radius"); }
enum Flipped { Point { k: i64 }, Circle { r: i64 } }
fn flipped(s: Flipped.Circle&) -> i64& { return s.r; }
fn flipped(s: Flipped.Point&) -> i64& { abort("a point has no radius"); }
fn diverging() {
    var s: Shape.. = Shape.Circle { r: 3 };
    let r .= radius(s);
    r = 5;
    print(r);
    var f: Flipped.. = Flipped.Circle { r: 4 };
    let q .= flipped(f);
    q = 6;
    print(q);
}

// A case that only returns null yields no reference, so the dispatched result
// is as writable as the reference the other case returns.
fn maybe_radius(s: Shape.Circle&) -> i64&? { return s.r; }
fn maybe_radius(s: Shape.Point&) -> i64&? { return null; }
fn widen(s: Shape..&) {
    let r: i64&? = maybe_radius(s);
    guard r else { print("no radius"); return; }
    r = 5;
    print(r);
}
fn null_case() {
    var c: Shape.. = Shape.Circle { r: 3 };
    widen(c);
    var p: Shape.. = Shape.Point { k: 4 };
    widen(p);
}

fn main() {
    diverging();
    null_case();
}
