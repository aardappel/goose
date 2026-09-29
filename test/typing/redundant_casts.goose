// warning: redundant `as i32`: a is already i32
// warning: redundant `as i64`: an i16 converts to i64 implicitly here
// warning: redundant `as u32`: 17 fits u32 here
// warning: redundant `as f32`: an i32 converts to f32 implicitly here
// warning: redundant `as f64`: an f32 converts to f64 implicitly here
// warning: redundant `as i64`: its constants 1 to 2 fit i64 here
// warning: redundant `as f64`: (n * 0.5) is an f64 here
// warning: redundant `as u32`: 1024 fits u32 here
// warning: redundant `as i16`: the cast to i64 around it converts b as well
// warning: redundant `as f32`: an i32 converts to f32 implicitly here
// warning: redundant `as i32`: a is already i32
// warning: redundant `as f32`: an i64 converts to f32 implicitly here
// warning: redundant `as f32`: an i32 converts to f32 implicitly here
// Casts that change nothing (§6.3): deleting each leaves the program meaning
// the same, and warns. Beside them are casts that decide a type, a value, an
// operation's width or a float's rounding, which do not; the results show
// the types they were computed at.

struct Pair { x: i32, y: f32 }

fn half(x: f32) -> f32 { x * 0.5 }
fn mix(x: u32) -> u32 { x * 2654435761 }
fn scaled(x: f32) -> f32 { x * 0.1 }
fn scaled(x: f64) -> f64 { x * 0.1 }
fn larger<T>(x: T, y: T) -> T { if x > y { x } else { y } }

// The types in a generic function's body can be another instantiation's, so
// its casts are not judged.
fn plus_one<T>(x: T) -> i64 { (x as i64) + 1 }

fn main() {
    let a: i32 = 100000;
    var s: i16 = 300;
    let b: u8 = 200;
    let n = 3;
    var v = 12345;

    // The operand has the type already, or converts to it where the cast
    // stands: a typed destination, an argument, an operator's other operand.
    let d: i32 = a as i32;
    let e: i64 = s as i64;
    let seed: u32 = 17 as u32;
    let f = half(a as f32);
    var g: f64 = 0.5;
    g += 0.25 as f32 as f64;
    let h: i64 = (if n > 2 { 1 } else { 2 }) as i64;
    let k: f64 = (n * 0.5) as f64;
    let m = mix(seed) % (1024 as u32);
    let o = (b as i16) as i64;
    print(d, " ", e, " ", seed, " ", f, " ", g, " ", h, " ", k, " ", m, " ", o);

    // A float of literals computes at the type it settles at: an f32 here,
    // as the cast has it.
    print(half((a as f32) * 0.5), " ", Pair { a as i32, (n as f32) * 0.5 }.y);

    // Casts that decide something stay: a type where nothing else gives one,
    // a narrowing, an operation's width, a float's rounding, a call's
    // overload or instantiation. Of two casts that cannot both go, the right
    // one warns.
    let u = 5 as u32;
    let small: u8 = (v % 10) as u8;
    let w = b as i32 * 2;
    let x: i64 = (a as i64) * a;
    let y: f64 = (a as f32) * 0.3;
    let z = scaled(a as f32);
    let big = larger(s as i64, e);
    let q = (a as f32) * (a as f32);
    print(u, " ", small, " ", w, " ", x, " ", y, " ", z, " ", big, " ", q, " ", plus_one(b));
    s += 1;
    v += 1;
}
