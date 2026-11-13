// runtime-debug
// An inlined call keeps the operand type its call site checked it at. A
// callee returning a narrower numeric type is implicitly widened to the
// common operand type (§6.3), and after TryInline unwraps its tail that
// conversion is still there: mixed-width arithmetic, bitwise operations and
// comparisons compute at the common type, and f32 computes at f32 precision
// before widening to f64. Values come from arg_count(), so the optimizer
// cannot fold the runtime cases; the `cf_` callees return constants, which
// exercises constant folding after inlining.

import std;
import os;

var byte_value:  u8  = 0;
var sbyte_value: i8  = 0;
var f32_value:   f32 = 0.0;

fn narrow_u8() -> u8 { return byte_value; }
fn narrow_i8() -> i8 { return sbyte_value; }
fn narrow_f32() -> f32 { return f32_value; }

fn cf_u8() -> u8 { return 250; }
fn cf_i8() -> i8 { return -100; }
fn cf_f32() -> f32 { return 0.1; }

fn main() {
    byte_value = (arg_count() + 249) as! u8;          // 250
    sbyte_value = (arg_count() + 155) as! i8;         // -100
    f32_value = (arg_count() / 10.0) as f32;          // 0.1 as f32

    let wide:   u32 = (arg_count() as u32) << 31 | 5; // 0x80000005
    let bits:   u32 = (arg_count() as u32) << 31 | 0xFA; // 0x800000FA
    let sbits:  i32 = 0x7FFFFF9C;
    let wide_f: f64 = arg_count() * 3.0;              // 3.0
    let low:    u8  = 250;
    let slow:   i8  = -100;
    let fv64:   f64 = f32_value;

    // Runtime values: a call returning a narrow type is the left operand.
    print("arith ",
          narrow_u8() + wide == low + wide, " ",
          narrow_u8() - wide == low - wide, " ",
          narrow_u8() * wide == low * wide, " ",
          narrow_u8() / wide == low / wide, " ",
          narrow_u8() % wide == low % wide);
    print("bit ",
          narrow_u8() & bits == low & bits, " ",
          narrow_i8() & sbits == slow & sbits, " ",
          narrow_u8() | bits == low | bits, " ",
          narrow_u8() ^ bits == low ^ bits);
    print("cmp ",
          (narrow_u8() < wide) == (low < wide), " ",
          (narrow_u8() > wide) == (low > wide), " ",
          (narrow_u8() <= wide) == (low <= wide), " ",
          (narrow_u8() >= wide) == (low >= wide), " ",
          (narrow_u8() == bits) == (low == bits), " ",
          (narrow_u8() != bits) == (low != bits));
    // The call on the right was always widened by the left operand's type.
    print("mirror ", wide + narrow_u8() == wide + low);
    // Shifts take the left operand's type, unchanged by inlining.
    print("shift ",
          (narrow_u8() << 3) == (low << 3), " ",
          (narrow_u8() >> 3) == (low >> 3));

    // f32 is computed at f32 precision, then widened to f64.
    print("f32 ",
          narrow_f32() + wide_f == fv64 + wide_f, " ",
          narrow_f32() - wide_f == fv64 - wide_f, " ",
          narrow_f32() * wide_f == fv64 * wide_f, " ",
          narrow_f32() / wide_f == fv64 / wide_f);
    // 0.1f < probe is true at f64, false when the compare rounds to f32.
    let probe: f64 = arg_count() * 0.100000005;
    print("f32cmp ", (narrow_f32() < probe) == (fv64 < probe));

    // Constant-returning callees: the fold after inlining uses the call
    // site's type, including its signedness.
    print("cfold ",
          cf_u8() + (0x80000005 as u32) == 250 + (0x80000005 as u32), " ",
          cf_u8() - (0x80000005 as u32) == 250 - (0x80000005 as u32), " ",
          cf_u8() * (0x80000005 as u32) == 250 * (0x80000005 as u32), " ",
          cf_u8() / (0x80000005 as u32) == 250 / (0x80000005 as u32), " ",
          cf_u8() % (0x80000005 as u32) == 250 % (0x80000005 as u32));
    print("cfbit ",
          cf_i8() & (0x7FFFFF9C as i32) == (-100) & (0x7FFFFF9C as i32), " ",
          cf_u8() | (0x800000FA as u32) == 250 | (0x800000FA as u32), " ",
          cf_u8() ^ (0x800000FA as u32) == 250 ^ (0x800000FA as u32));
    print("cfcmp ",
          (cf_u8() < (-1 as i16)) == (250 < (-1 as i16)), " ",
          (cf_u8() <= (-1 as i16)) == (250 <= (-1 as i16)), " ",
          (cf_u8() > (-1 as i16)) == (250 > (-1 as i16)), " ",
          (cf_u8() >= (-1 as i16)) == (250 >= (-1 as i16)), " ",
          (cf_u8() == (-1 as i16)) == (250 == (-1 as i16)), " ",
          (cf_u8() != (-1 as i16)) == (250 != (-1 as i16)));
    // A constant f32 widens with the f32 rounding the source had.
    print("cff32 ", cf_f32() + wide_f == fv64 + wide_f);
}
