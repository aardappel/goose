// A call whose narrower result an operation widens (§6.3) still computes at
// the wider type once it is inlined, as the left operand as well as the
// right one: u16 0xFFFF * u32 0xFFFF is 4294836225, not 1 (#313).
import std;
import os;

var regs:  u16[8] = [0; 8];
var bytes: u8[4]  = [0; 4];

fn reg(i: u8) -> u16 { return regs[i & 7]; }
fn byte(i: u8) -> u8 { return bytes[i & 3]; }
fn narrow() -> u16 { return 0xFFFF; }
fn half(x: f32) -> f32 { return x * 0.5; }

fn main() {
    // Run-time values, not constants.
    regs[0] = (arg_count() + 0xFFFE) as! u16;
    bytes[0] = (arg_count() + 0xFE) as! u8;
    let wide: u32 = regs[0];
    let w16:  u16 = bytes[0];
    let w64:  u64 = regs[0];
    let a:    u16 = reg(0);
    print(reg(0) * wide, " ", a * wide, " ", wide * reg(0));
    print(reg(0) + wide);
    print(byte(0) * w16);
    print(reg(0) * w64);
    let x: u32 = reg(0) * wide;
    print(x);
    print(narrow() * wide);
    let d: f64 = 16777217.0;
    print(half(1.0) + d);
}
