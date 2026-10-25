// The Goose standard library: `math` (docs/stdlib.md): libm, declared once
// per function and float width (spec §7.10), plus the constants and
// conversions that need no C. floor/ceil/round/trunc return floats, as in C;
// `as i64` converts. Float `%` is already fmod (§6.1). sqrt is the runtime's
// (runtime_ext.h), which has no errno to set and so compiles to the bare
// square root instruction where the C compiler allows. The functions C has
// set errno are libm's under the runtime's names, which tell the C compiler
// they depend on their arguments alone (Goose has no errno).

extern fn sqrt(x: f64) -> f64 = "gs_sqrt";            extern fn sqrt(x: f32) -> f32 = "gs_sqrtf";
extern fn sin(x: f64) -> f64 = "gs_sin";              extern fn sin(x: f32) -> f32 = "gs_sinf";
extern fn cos(x: f64) -> f64 = "gs_cos";              extern fn cos(x: f32) -> f32 = "gs_cosf";
extern fn tan(x: f64) -> f64 = "gs_tan";              extern fn tan(x: f32) -> f32 = "gs_tanf";
extern fn asin(x: f64) -> f64 = "gs_asin";            extern fn asin(x: f32) -> f32 = "gs_asinf";
extern fn acos(x: f64) -> f64 = "gs_acos";            extern fn acos(x: f32) -> f32 = "gs_acosf";
extern fn atan(x: f64) -> f64 = "gs_atan";            extern fn atan(x: f32) -> f32 = "gs_atanf";
extern fn atan2(y: f64, x: f64) -> f64 = "gs_atan2";  extern fn atan2(y: f32, x: f32) -> f32 = "gs_atan2f";
extern fn exp(x: f64) -> f64 = "gs_exp";              extern fn exp(x: f32) -> f32 = "gs_expf";
extern fn log(x: f64) -> f64 = "gs_log";              extern fn log(x: f32) -> f32 = "gs_logf";
extern fn log2(x: f64) -> f64 = "gs_log2";            extern fn log2(x: f32) -> f32 = "gs_log2f";
extern fn log10(x: f64) -> f64 = "gs_log10";          extern fn log10(x: f32) -> f32 = "gs_log10f";
extern fn pow(x: f64, y: f64) -> f64 = "gs_pow";      extern fn pow(x: f32, y: f32) -> f32 = "gs_powf";
// x * y + z, rounded once where the code is built for FMA and twice where it
// is not: which is the implementation's, so results may differ between
// machines (§7.12). A simd fn's versions past the baseline round once.
extern fn mul_add(x: f64, y: f64, z: f64) -> f64 = "gs_mul_add";
extern fn mul_add(x: f32, y: f32, z: f32) -> f32 = "gs_mul_addf";
extern fn floor(x: f64) -> f64;                       extern fn floor(x: f32) -> f32 = "floorf";
extern fn ceil(x: f64) -> f64;                        extern fn ceil(x: f32) -> f32 = "ceilf";
extern fn round(x: f64) -> f64;                       extern fn round(x: f32) -> f32 = "roundf";
extern fn trunc(x: f64) -> f64;                       extern fn trunc(x: f32) -> f32 = "truncf";

let PI = 3.141592653589793;
let TAU = 6.283185307179586;

fn radians(deg: f64) -> f64 { deg * (PI / 180.0) }
fn degrees(rad: f64) -> f64 { rad * (180.0 / PI) }

fn is_nan(x: f64) -> bool { x != x }
fn is_inf(x: f64) -> bool { x == x && (x - x) != (x - x) }
