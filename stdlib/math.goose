// The Goose standard library: `math` (docs/stdlib.md): libm, declared once
// per function and float width (spec §7.10), plus the constants and
// conversions that need no C. floor/ceil/round/trunc return floats, as in C;
// `as i64` converts. Float `%` is already fmod (§6.1).

extern fn sqrt(x: f64) -> f64;      extern "sqrtf" fn sqrt(x: f32) -> f32;
extern fn sin(x: f64) -> f64;       extern "sinf" fn sin(x: f32) -> f32;
extern fn cos(x: f64) -> f64;       extern "cosf" fn cos(x: f32) -> f32;
extern fn tan(x: f64) -> f64;       extern "tanf" fn tan(x: f32) -> f32;
extern fn asin(x: f64) -> f64;      extern "asinf" fn asin(x: f32) -> f32;
extern fn acos(x: f64) -> f64;      extern "acosf" fn acos(x: f32) -> f32;
extern fn atan(x: f64) -> f64;      extern "atanf" fn atan(x: f32) -> f32;
extern fn atan2(y: f64, x: f64) -> f64;   extern "atan2f" fn atan2(y: f32, x: f32) -> f32;
extern fn exp(x: f64) -> f64;       extern "expf" fn exp(x: f32) -> f32;
extern fn log(x: f64) -> f64;       extern "logf" fn log(x: f32) -> f32;
extern fn log2(x: f64) -> f64;      extern "log2f" fn log2(x: f32) -> f32;
extern fn log10(x: f64) -> f64;     extern "log10f" fn log10(x: f32) -> f32;
extern fn pow(x: f64, y: f64) -> f64;     extern "powf" fn pow(x: f32, y: f32) -> f32;
extern fn floor(x: f64) -> f64;     extern "floorf" fn floor(x: f32) -> f32;
extern fn ceil(x: f64) -> f64;      extern "ceilf" fn ceil(x: f32) -> f32;
extern fn round(x: f64) -> f64;     extern "roundf" fn round(x: f32) -> f32;
extern fn trunc(x: f64) -> f64;     extern "truncf" fn trunc(x: f32) -> f32;

let PI = 3.141592653589793;
let TAU = 6.283185307179586;

fn radians(deg: f64) -> f64 { deg * (PI / 180.0) }
fn degrees(rad: f64) -> f64 { rad * (180.0 / PI) }

fn is_nan(x: f64) -> bool { x != x }
fn is_inf(x: f64) -> bool { x == x && (x - x) != (x - x) }
