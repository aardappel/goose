// error: a list of patterns binds no payload
// Listed variants' payloads differ in type (§8.1), so no one binder could
// take them; a variant that binds its payload has an arm of its own.
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

fn main() {
    let s = Shape.Dot;
    print(match s { Dot, Rect r => r.w, _ => 0.0 });
}
