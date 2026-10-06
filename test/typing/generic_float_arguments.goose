// runtime-debug
// warning: redundant `as f32`: an i32 converts to f32 implicitly here
// Explicit generic arguments choose the arithmetic width. A result cast
// rounds after the computation; contextual_generic_floats checks f32 context.
import std;

fn same<T>(value: T) -> T { value }
fn width(value: f32) -> u8[:] { "f32" }
fn width(value: f64) -> u8[:] { "f64" }
fn read<T>(value: const T&) -> T { value }

fn main() {
    var pixels: i32 = 1280;
    var rows: i32 = 800;
    var n: i32 = 9;

    // An explicit choice for a nested generic expression: the innermost
    // call gets its type explicitly, and its f32 result types the outer one.
    let scale: f32 = max(0.5, min<f32>(pixels / 960.0, rows / 600.0));
    let rounded_scale: f32 = max(0.5, min(pixels / 960.0, rows / 600.0)) as f32;
    print(scale, " ", rounded_scale);

    // These alternatives can differ: arithmetic at f32 versus f64 followed
    // by a single rounding. A cast on a computed expression must stay.
    let narrow: f32 = max(0.5, min<f32>(n * 0.1, 1.0));
    let rounded: f32 = max(0.5, min(n * 0.1, 1.0)) as f32;
    let cast_arg = same<f32>((n * 0.1) as f32);
    print(narrow, " ", rounded, " ", cast_arg);

    // Literal-like lets retain their stored f64 value and round at use.
    let stored = n * 0.1;
    print(same<f32>(stored), " ", same<f32>(n * 0.1));

    // Explicit f64 values, reference binding and overload selection retain
    // their existing meanings even beside literal-like expressions.
    let wide: f64 = 0.5;
    let result = max(wide, n * 0.1);
    print(width(result), " ", width(read(result)), " ", width(same(0.1)));
    print(width(n as f32), " ", width(n * 0.1), " ", width(same<f32>(0.1)));

    // Both casts together anchor f32 inference. Only the later cast warns;
    // deleting that warning leaves the earlier one to choose f32. Deleting
    // both would leave this annotated destination to choose f32 instead.
    let anchored: f32 = min((pixels as f32) / 960.0, (rows as f32) / 600.0);
    let repaired: f32 = min((pixels as f32) / 960.0, rows / 600.0);
    print(anchored, " ", repaired);
}
