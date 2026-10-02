// warning: redundant `as f32`: K is an f32 here
// An untyped `let`, local or global, whose initializer is a float of
// literals and integers stays literal-like (§6.3): it holds its value as an
// f64, and each use adapts to f32 as the literal written there would, at a
// typed destination, as an operand, in a construct's branches and as a
// generic argument beside an f32. Where nothing gives it a type it is an
// f64. Nine tenths show the width: an f64 0.9 prints as 0.9, rounded to f32
// it prints as 0.89999997615814209, and 9 * 0.1 computed in f32 gives
// 0.90000003576278687.
import std;
import math;

let K = 0.25;
let HALF_K = K * 2;
let TYPED: f64 = 0.5;

struct V3 { x: f32, y: f32, z: f32 }

fn half(x: f32) -> f32 { x * 0.5 }
fn id64(x: f64) -> f64 { x }
fn twice(x: f64&) -> f64 { x * 2 }
fn same<T>(x: T) -> T { x }

fn main() {
    var n = 9;
    let r: f32 = 1.5;

    // A local from integers and a literal: an f64 that rounds at each f32 use.
    let a = n * 0.1;
    let v = V3 { a, a * r, -a };
    print(a, " ", v.x, " ", v.y, " ", v.z);
    print(half(a), " ", id64(a), " ", n * 0.1 * r);

    // Constants, global and local, and one computed from another.
    let q = 0.75;
    print(K * r, " ", HALF_K * r, " ", half(q), " ", q, " ", HALF_K);

    // std's PI is such a global: an f32 argument takes it rounded.
    print(half(PI), " ", sin(half(PI)), " ", sin(PI / 2.0));

    // A construct of them is literal-like as a whole; with an f32 branch it
    // is an f32.
    let c = n > 3;
    let pick = if c { a } else { 0.5 };
    let both = if c { a } else { r };
    print(half(pick), " ", both);

    // Generic arguments: the typed one binds, as with a literal; alone it
    // binds f64.
    print(max(a, r), " ", min(r, q), " ", same(a));

    // In a loop and in a nested function.
    var sum: f32 = 0.0;
    for i in 3 { let step = i * K; sum += step * r; }
    fn scaled() -> f32 { a * 2 }
    print(sum, " ", scaled());

    // Storage of its own: a reference reads the f64, and an f64& argument
    // takes it (where no use relies on its value, §4.4). A cast it does not
    // need is redundant.
    let d = 0.75;
    let p = &d;
    print(p, " ", twice(d), " ", half(K as f32));

    // A typed global, a var and a typed local are values of their type.
    var w = 0.5;
    w += 1.0;
    let t: f64 = 0.5;
    print(TYPED + r, " ", w + r, " ", t * r);
}
