// runtime-debug: infinities and NaNs have one text form (§3.7) on every
// backend and C library: inf, -inf and nan, whatever the NaN's sign bit.
// Every value comes from an array at run time, so no C compiler folds it.
import std;

struct Hit { at: f64, w: f32 }
enum Reading { Missing, Value { v: f32 } }
struct Wrapped { v: f64 }
fn format(out: u8[>..]&, w: const Wrapped&) { format(out, "<", w.v, ">"); }

fn main() {
    var src: f64[>..] = [0.0, 1.0];
    let z = src[0];
    let one = src[1];
    let inf = one / z;
    let ninf = -one / z;
    let nan = z / z;
    let nnan = -nan;
    print(inf, " ", ninf, " ", nan, " ", nnan);
    var src32: f32[>..] = [0.0, 1.0];
    let z32 = src32[0];
    let inf32 = src32[1] / z32;
    let nan32 = z32 / z32;
    print(inf32, " ", -inf32, " ", nan32, " ", -nan32);
    print(inf as f32, " ", nan as f32, " ", inf32 as f64, " ", -nan32 as f64);

    print(str(inf, ",", ninf, ",", nan, ",", nnan, ",", inf32, ",", nan32));
    var text: u8[>..] = [];
    format(text, ninf, " ", nnan, " ", -inf32);
    print(text);

    print([inf, ninf, nan, one], " ", [inf32, nan32]);
    print(Hit { inf, -nan32 }, " ", Hit { nan, -inf32 });
    print(Reading.Value { inf32 }, " ", Reading.Value { nan32 });
    let maybe: f64? = ninf;
    print(maybe, " ", Wrapped { nan }, " ", [Wrapped { inf }]);

    var fixed: u8[>..] = [];
    format_flt(fixed, inf, 2);
    fixed.push(' ');
    format_flt(fixed, ninf, 0);
    fixed.push(' ');
    format_flt(fixed, nnan, 3);
    fixed.push(' ');
    format_flt(fixed, -one / 3.0, 2);
    print(fixed);
}
