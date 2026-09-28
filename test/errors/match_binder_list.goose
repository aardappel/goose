// error: a list of patterns binds no payload
// A variant that binds its payload stands alone in its arm (§8.1), so no
// other pattern follows the binder.
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

fn main() {
    let s = Shape.Dot;
    print(match s { Rect r, Dot => 1, _ => 0 });
}
