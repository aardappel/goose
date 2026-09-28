// A construct whose branches are all literals is itself literal-like (§6.4):
// integer constants adapt to any integer type they all fit, and floats of
// literals, with the integers among them, to either float width, where the
// construct meets a parameter or another operand as much as at a typed
// destination. A literal branch beside a typed one takes that type. u8
// arithmetic wrapping, and f32 rounding a tenth, show the type each got:
// 9 * 0.1 computed at f32 prints 0.90000003576278687, and 0.89999997615814209
// were it computed at f64 and then rounded.
import std;

fn g(x: i32) -> i32 { x }
fn k(x: f32) -> f32 { x }
fn u(x: u8) -> u8 { x }
fn h(x: u8) -> u8[:] { "u8" }
fn h(x: f64) -> u8[:] { "f64" }

fn main() {
    var flags: bool[>..] = [true, false];
    let yes = flags[0];
    let no = flags[1];
    var x32: f32 = 0.5;
    var x8: u8 = 5;
    var n: i32 = 9;
    var sel: i64 = 1;

    // All-literal branches adapt to the parameter they meet.
    print(g(if yes {1} else {2}), " ", u(if no {1} else {250}));   // 1 250
    print(k(if yes {0.1} else {0.25}), " ", k(if no {1} else {2})); // 0.10000000149011612 2
    print(h(if yes {1} else {2}), " ", h(if yes {0.5} else {1}));   // u8 f64
    print(max(if yes {100} else {50}, x8) * 3);     // 44

    // An operand gives them its type.
    print((if yes {100} else {50}) * x8, " ", (if yes {0.1} else {0.25}) * x32);  // 244 0.05000000074505806

    // A literal branch beside a typed one takes its type.
    let a = if no { x32 } else { 0.62 };
    let b = if no { x8 } else { 100 };
    print(a, " ", b * 3);                           // 0.62000000476837158 44
    let z = match sel { 0 => 1, 1 => 100, _ => x8 };
    print(z * 3);                                   // 44
    let tenths = if no { x32 } else { n * 0.1 };
    print(tenths);                                  // 0.90000003576278687

    // Nested constructs, blocks and breaks.
    let nested = if yes { if no {1} else {100} } else { x8 };
    print(nested * 3);                              // 44
    print(k(block { if no { break 0.1; } 0.2 }));   // 0.20000000298023224
    let early = block { if no { break x8; } 100 };
    print(early * 3);                               // 44
    print((block { if yes { break n * 0.1; } 0.25 }) * x32);   // 0.45000001788139343
    let l = loop { if no { break 1; } break 2.5; };
    print(l);                                       // 2.5

    // Floats of literals and integers: the width follows the destination,
    // and is f64 where nothing gives one.
    print(k(if yes { n * 0.1 } else { 0.25 }), " ", k(if no { n } else { 0.1 }));  // 0.90000003576278687 0.10000000149011612
    let f = if yes { n * 0.1 } else { 0.25 };
    print(f);                                       // 0.9
}
