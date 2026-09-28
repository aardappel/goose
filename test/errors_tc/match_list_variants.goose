// error: a list of patterns matches integers only
// Only an integer match lists several values in one arm (§8.1); an ADT
// match arm names one variant, which a binder may take the payload of.
enum Shape { Circle, Square, Point }

fn main() {
    let s = Shape.Point;
    print(match s { Circle, Square => 1, Point => 0 });
}
