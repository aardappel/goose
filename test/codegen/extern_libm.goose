// extern fns binding C99 <math.h> functions in both widths, most of them
// ones the math module does not wrap. A run in-process resolves only the
// libm functions the compiler registers (src/jit_runtime.c) and declares
// (src/runtime/jit_c.h), so each must link there and return the right
// value, which C's implicit int return for an undeclared function would
// not. The arguments give results exact in every libm, but for tgamma,
// which is compared within a tolerance. lrint and its kin are left out:
// they return long or long long, and no one Goose type is the same C type
// as both on every platform, which a prototype beside <math.h>'s must be.

extern fn acosh(x: f64) -> f64;
extern fn asinh(x: f64) -> f64;
extern fn atanh(x: f64) -> f64;
extern fn erf(x: f64) -> f64;
extern fn erfc(x: f64) -> f64;
extern fn tgamma(x: f64) -> f64;
extern fn lgamma(x: f64) -> f64;
extern fn logb(x: f64) -> f64;
extern fn rint(x: f64) -> f64;
extern fn nearbyint(x: f64) -> f64;
extern fn fmin(x: f64, y: f64) -> f64;
extern fn fmax(x: f64, y: f64) -> f64;
extern fn fdim(x: f64, y: f64) -> f64;
extern fn nextafter(x: f64, y: f64) -> f64;
extern fn remainder(x: f64, y: f64) -> f64;
extern fn fma(x: f64, y: f64, z: f64) -> f64;
extern fn scalbn(x: f64, n: i32) -> f64;
extern fn ilogb(x: f64) -> i32;
extern fn modf(x: f64, ip: f64&) -> f64;
extern fn remquo(x: f64, y: f64, q: i32&) -> f64;
extern fn cbrt(x: f64) -> f64;
extern fn hypot(x: f64, y: f64) -> f64;
extern fn expm1(x: f64) -> f64;
extern fn fabs(x: f64) -> f64;

extern fn acoshf(x: f32) -> f32;
extern fn asinhf(x: f32) -> f32;
extern fn atanhf(x: f32) -> f32;
extern fn erff(x: f32) -> f32;
extern fn erfcf(x: f32) -> f32;
extern fn tgammaf(x: f32) -> f32;
extern fn lgammaf(x: f32) -> f32;
extern fn logbf(x: f32) -> f32;
extern fn rintf(x: f32) -> f32;
extern fn nearbyintf(x: f32) -> f32;
extern fn fminf(x: f32, y: f32) -> f32;
extern fn fmaxf(x: f32, y: f32) -> f32;
extern fn fdimf(x: f32, y: f32) -> f32;
extern fn nextafterf(x: f32, y: f32) -> f32;
extern fn remainderf(x: f32, y: f32) -> f32;
extern fn fmaf(x: f32, y: f32, z: f32) -> f32;
extern fn scalbnf(x: f32, n: i32) -> f32;
extern fn ilogbf(x: f32) -> i32;
extern fn modff(x: f32, ip: f32&) -> f32;
extern fn remquof(x: f32, y: f32, q: i32&) -> f32;
extern fn cbrtf(x: f32) -> f32;
extern fn hypotf(x: f32, y: f32) -> f32;
extern fn expm1f(x: f32) -> f32;
extern fn fabsf(x: f32) -> f32;

fn main() {
    print(acosh(1.0), " ", asinh(0.0), " ", atanh(0.0));
    print(erf(0.0), " ", erfc(0.0), " ", fabs(tgamma(5.0) - 24.0) < 1e-12, " ", lgamma(1.0));
    print(logb(48.0), " ", rint(2.5), " ", nearbyint(-3.5));
    print(fmin(2.0, -1.0), " ", fmax(2.0, -1.0), " ", fdim(5.0, 3.0), " ", fdim(3.0, 5.0));
    print(nextafter(1.0, 2.0) - 1.0 == 1.0 / 4503599627370496.0, " ", remainder(7.0, 2.0));
    // Fused: 2^-30 survives, where a separate multiply rounds it away.
    let e = 1.0 / 1073741824.0;
    print(fma(1.0 + e, 1.0 - e, -1.0) == -(e * e), " ", fma(2.0, 3.0, 4.0));
    print(scalbn(3.0, 4), " ", ilogb(1000.0));
    var ip = 0.0;
    let frac = modf(-3.25, ip);
    var q: i32 = 0;
    let r = remquo(10.0, 3.0, q);
    print(frac, " ", ip, " ", r, " ", q);
    print(cbrt(27.0), " ", hypot(3.0, 4.0), " ", expm1(0.0));

    let one: f32 = 1.0;
    let zero: f32 = 0.0;
    print(acoshf(one), " ", asinhf(zero), " ", atanhf(zero));
    print(erff(zero), " ", erfcf(zero), " ", fabsf(tgammaf(5.0) - 24.0) < 1e-5, " ", lgammaf(one));
    print(logbf(48.0), " ", rintf(2.5), " ", nearbyintf(-3.5));
    print(fminf(2.0, -1.0), " ", fmaxf(2.0, -1.0), " ", fdimf(5.0, 3.0), " ", fdimf(3.0, 5.0));
    let ulpf: f32 = 1.0 / 8388608.0;
    print(nextafterf(one, 2.0) - one == ulpf, " ", remainderf(7.0, 2.0));
    let ef: f32 = 1.0 / 4096.0;
    print(fmaf(one + ef, one - ef, -one) == -(ef * ef), " ", fmaf(2.0, 3.0, 4.0));
    print(scalbnf(3.0, 4), " ", ilogbf(1000.0));
    var ipf: f32 = 0.0;
    let fracf = modff(-3.25, ipf);
    var qf: i32 = 0;
    let rf = remquof(10.0, 3.0, qf);
    print(fracf, " ", ipf, " ", rf, " ", qf);
    print(cbrtf(27.0), " ", hypotf(3.0, 4.0), " ", expm1f(zero));
}
