// runtime-debug
// An f32 destination selects the width of a still-unconstrained floating
// generic computation, including directly nested calls to the same kind.
import std;

fn same<T>(value: T) -> T { value }
fn width(value: f32) -> u8[:] { "f32" }
fn width(value: f64) -> u8[:] { "f64" }
fn scale(width: i32, height: i32) -> f32 {
    max(0.25, min(width / 960.0, height / 600.0))
}
struct Scale { value: f32 }

// Context must not choose either of these concrete overloads. The natural
// f64 argument chooses the generic function in either declaration order.
fn choice<T>(value: T) -> T { value }
fn choice(value: f32) -> f32 { 17.0 }
fn reversed(value: f32) -> f32 { 17.0 }
fn reversed<T>(value: T) -> T { value }

fn main() {
    var pixels: i32 = 1280;
    var rows: i32 = 800;
    var n: i32 = 9;
    let a: f32 = max(0.25, min(pixels / 960.0, rows / 600.0));
    let b: f32 = max(min(rows / 600.0, pixels / 960.0), 0.25);
    let c: f32 = same(max(0.25, same(min(pixels / 960.0, rows / 600.0))));
    let record = Scale { value: min(pixels / 960.0, rows / 600.0) };
    var assigned: f32 = 0.0;
    assigned = min(pixels / 960.0, rows / 600.0);
    print(a, " ", b, " ", c, " ", scale(pixels, rows));
    print(record.value, " ", assigned);

    // UFCS is the same call, including a nested receiver checked again at
    // its selected parameter type. A branch also forwards its destination.
    let receiver: f32 = min(pixels / 960.0, rows / 600.0).max(0.25);
    let branch: f32 = if n > 0 { min(pixels / 960.0, rows / 600.0) } else { 1.0 };
    print(receiver, " ", branch);

    // Computation at f32 differs from f64 followed by a single rounding.
    let narrow: f32 = max(0.5, min(n * 0.1, 1.0));
    let rounded: f32 = max(0.5, min(n * 0.1, 1.0)) as f32;
    let explicit: f32 = max(0.5, min<f32>(n * 0.1, 1.0));
    print(narrow, " ", rounded, " ", explicit);

    // Integer literals can convert after a float literal supplies a width;
    // their position cannot choose a different generic specialization.
    let left: f32 = min(9, 0.5);
    let right: f32 = min(0.5, 9);
    let whole: f32 = same(n / 10);
    print(left, " ", right, " ", whole);

    // No destination, a cast, an inferred binding, and overload selection
    // retain their usual f64 defaults. An f64 destination stays f64 too.
    let stored = min(n * 0.1, 1.0);
    let wide: f64 = max(0.5, min(n * 0.1, 1.0));
    print(width(stored), " ", width(wide), " ", width(min(n * 0.1, 1.0)));
    print(choice(0.5), " ", reversed(0.5));
    print(width(same<f32>(0.5)), " ", width(same<f64>(0.5)));
}
