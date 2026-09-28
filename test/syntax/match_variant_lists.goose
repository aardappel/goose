// dump-runtime: an ADT match arm may list several variants (§8.1), with a
// payload or without, any one of which selects the arm; it binds no payload,
// but a lone variant beside it still does. Lists match fixed-mode values, a
// reference and variable-mode elements, and a generic function's list reads
// its names as variants for an enum and as constants for an integer.
enum Shape {
    Circle { r: f64 },
    Rect { w: f64, h: f64 },
    Tri { a: f64, b: f64, c: f64 },
    Dot,
    Line { l: f64 },
}

enum Dir { North, East, South, West }

let North = 7;
let East = 8;

fn axis(d: Dir) -> u8[:] { match d { North, South => "vertical", East, West => "horizontal" } }

fn curved(s: Shape) -> bool { match s { Circle => true, Rect, Tri, Dot, Line => false } }

fn corners(s: Shape) -> i64 {
    match s { Rect r => if r.w == r.h { 40 } else { 4 }, Tri, Dot => 3, _ => 0 }
}

fn viaref(s: Shape&) -> i64 { match s { Circle, Line => 1, Dot => 2, _ => 3 } }

fn kind(x) -> i64 { match x { North, East => 1, _ => 0 } }

fn main() {
    let ds: Dir[4] = [Dir.North, Dir.East, Dir.South, Dir.West];
    for d in ds { print(axis(d)); }
    var shapes: Shape[>..] = [];
    shapes.push(Shape.Circle { r: 1.0 });
    shapes.push(Shape.Rect { w: 2.0, h: 3.0 });
    shapes.push(Shape.Tri { a: 3.0, b: 4.0, c: 5.0 });
    shapes.push(Shape.Dot);
    shapes.push(Shape.Line { l: 2.0 });
    shapes.push(Shape.Rect { w: 2.0, h: 2.0 });
    for s in shapes { print(curved(s), " ", corners(s), " ", viaref(s)); }
    var packed: Shape..[>..] = [];
    packed.push(Shape.Circle { r: 1.0 });
    packed.push(Shape.Dot);
    packed.push(Shape.Rect { w: 2.0, h: 3.0 });
    packed.push(Shape.Line { l: 5.0 });
    var n = 0;
    for s in packed {
        match s {
            Dot, Line => block { n += 10; },
            Rect &r => block { r.w += 1.0; n += 100; },
            _ => block { n += 1; },
        }
    }
    print(n, " ", packed);                  // 121 [.., Rect { 3, 3 }, ..]
    print(kind(Dir.North), kind(Dir.East), kind(Dir.South), kind(7), kind(8), kind(9));
                                            // 110110
}
