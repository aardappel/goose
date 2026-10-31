// warning: comparison is always false: u32 values are 0 to 4294967295
// warning: comparison is always true: u32 values are 0 to 4294967295
// warning: comparison is always false: u32 values are 0 to 4294967295
// warning: comparison is always true: u8 values are 0 to 255
// warning: comparison is always false: i8 values are -128 to 127
// An ordering comparison of an integer with a constant that the integer's
// type alone decides is a warning: `e < 0` of an unsigned `e` is always
// false, as is `0 > e` of an index type. A comparison some value of the
// type could go either way, a u64's above i64, and generic code, where
// another specialization may make it matter, are left alone.
import std;

var nodes: f64[>..];
type Id = u32 in nodes;

fn below(e: u32) -> bool { e < 0 }
fn above(e: u32) -> bool { e >= 0 }
fn id_below(e: Id) -> bool { 0 > e }
fn byte_top(b: u8) -> bool { b <= 255 }
fn least(x: i8) -> bool { x < -128 }
fn wide(e: u64) -> bool { e > 100 }
fn zero(e: u64) -> bool { e <= 0 }
fn signed(x: i64) -> bool { x < 0 }
fn one(e: u32) -> bool { e < 1 }
fn negative<T>(x: T) -> bool { x < 0 }

fn main() {
    nodes.push(1.0);
    print(below(1), " ", above(1), " ", id_below(0 as Id), " ", byte_top(7), " ", least(1));
    print(wide(1), " ", zero(0), " ", signed(-1), " ", one(0));
    print(negative(1 as u32), " ", negative(-1));
}
