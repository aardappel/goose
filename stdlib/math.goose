// The Goose standard library: `math` (docs/stdlib.md): libm, declared once
// per function and float width (spec §7.10), plus the constants and
// conversions that need no C. floor/ceil/round/trunc return floats, as in C;
// `as i64` converts. Float `%` is already fmod (§6.1). sqrt is the runtime's
// (runtime_ext.h), which has no errno to set and so compiles to the bare
// square root instruction where the C compiler allows. The functions C has
// set errno are libm's under the runtime's names, which tell the C compiler
// they depend on their arguments alone (Goose has no errno).

extern "gs_sqrt" fn sqrt(x: f64) -> f64;            extern "gs_sqrtf" fn sqrt(x: f32) -> f32;
extern "gs_sin" fn sin(x: f64) -> f64;              extern "gs_sinf" fn sin(x: f32) -> f32;
extern "gs_cos" fn cos(x: f64) -> f64;              extern "gs_cosf" fn cos(x: f32) -> f32;
extern "gs_tan" fn tan(x: f64) -> f64;              extern "gs_tanf" fn tan(x: f32) -> f32;
extern "gs_asin" fn asin(x: f64) -> f64;            extern "gs_asinf" fn asin(x: f32) -> f32;
extern "gs_acos" fn acos(x: f64) -> f64;            extern "gs_acosf" fn acos(x: f32) -> f32;
extern "gs_atan" fn atan(x: f64) -> f64;            extern "gs_atanf" fn atan(x: f32) -> f32;
extern "gs_atan2" fn atan2(y: f64, x: f64) -> f64;  extern "gs_atan2f" fn atan2(y: f32, x: f32) -> f32;
extern "gs_exp" fn exp(x: f64) -> f64;              extern "gs_expf" fn exp(x: f32) -> f32;
extern "gs_log" fn log(x: f64) -> f64;              extern "gs_logf" fn log(x: f32) -> f32;
extern "gs_log2" fn log2(x: f64) -> f64;            extern "gs_log2f" fn log2(x: f32) -> f32;
extern "gs_log10" fn log10(x: f64) -> f64;          extern "gs_log10f" fn log10(x: f32) -> f32;
extern "gs_pow" fn pow(x: f64, y: f64) -> f64;      extern "gs_powf" fn pow(x: f32, y: f32) -> f32;
extern fn floor(x: f64) -> f64;                     extern "floorf" fn floor(x: f32) -> f32;
extern fn ceil(x: f64) -> f64;                      extern "ceilf" fn ceil(x: f32) -> f32;
extern fn round(x: f64) -> f64;                     extern "roundf" fn round(x: f32) -> f32;
extern fn trunc(x: f64) -> f64;                     extern "truncf" fn trunc(x: f32) -> f32;

let PI = 3.141592653589793;
let TAU = 6.283185307179586;

fn radians(deg: f64) -> f64 { deg * (PI / 180.0) }
fn degrees(rad: f64) -> f64 { rad * (180.0 / PI) }

fn is_nan(x: f64) -> bool { x != x }
fn is_inf(x: f64) -> bool { x == x && (x - x) != (x - x) }
