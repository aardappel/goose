// error: cannot pass the payload of fixed-mode Shape by reference to a case of area
// error: take it by value: Shape.Rect
// A fixed-mode scrutinee dispatches to by-value variant parameters only
// (§3.5, §8.2): the variant may be overwritten while a case runs.
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 } }
fn area(c: Shape.Circle) -> f64 { c.r * c.r * 3.0 }
fn area(r: Shape.Rect&) -> f64 { r.w * r.h }
fn main() {
    var shapes: Shape[>..] = [];
    shapes.push(Shape.Rect { w: 2.0, h: 3.0 });
    print(area(shapes[0]));
}
