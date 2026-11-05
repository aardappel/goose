// const if in match arm lists (const_for.md §11.4): filtered variants beside
// written arms, arms beside _, and a constant list's arms under a const if.
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }
let DETAIL = true;

fn area(c: Shape.Circle) -> i64 { c.r * c.r * 3 }
fn area(r: Shape.Rect) -> i64 { r.w * r.h }

fn payload_area(s: Shape) -> i64 {
    match s {
        const for <N, V> in variants<Shape> {
            const if !same<V, Shape.Dot> { V v => area(v), }
        },
        Dot => -1,
    }
}
fn name(n: i64) -> const u8[:] {
    match n {
        const if DETAIL { 0 => "zero", const for <C> in [1, 2, 4] { C => "power", } }
        else { 0, 1, 2, 4 => "small" },
        _ => "other",
    }
}
fn kind(s: Shape) -> i64 {
    match s {
        const for <N, V> in variants<Shape> {
            const if N == "Dot" { V => 0, } else const if N == "Rect" { V r => r.w, } else { V c => c.r, }
        }
    }
}
fn main() {
    print(payload_area(Shape.Circle { r: 2 }), " ", payload_area(Shape.Rect { w: 2, h: 5 }), " ",
          payload_area(Shape.Dot));
    print(name(0), " ", name(2), " ", name(3));
    print(kind(Shape.Dot), " ", kind(Shape.Rect { w: 7, h: 1 }), " ", kind(Shape.Circle { r: 9 }));
}
