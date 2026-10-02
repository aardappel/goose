// warning: redundant `as u8`: SZ is 15, which fits u8 here
// warning: redundant `as i32`: (B + 1) is 31, which fits i32 here
// An untyped `let` or `const` global with an integer constant initializer is
// a named constant (§3.1): each use adapts as the literal would, at a typed
// destination, as an operand, in a construct's branches and as a generic
// argument, and is an i64 where nothing gives it a type. One named constant
// may be computed from another. u8 arithmetic wrapping shows the type an
// operand took. A typed global, and a local `let`, keep their own types.
import std;

let SZ = 15;
let NEG = -3;
let B = SZ * 2;
const MASK = 0xF0;
let HUGE = 18446744073709551615;
let TYPED: i64 = 7;

struct Item { len: u8, dir: i8, id: i32 }

fn add(x: u8, by) -> u8 { x + by }
fn takes_u16(x: u16) -> u16 { x + 1 }
fn peek(x: const i64&) -> i64 { x }

fn main() {
    let it = Item { len: SZ, dir: NEG, id: B };
    print(it.len, " ", it.dir, " ", it.id);

    // Operands: an 8-bit add and mask, a u64 compare, u64 storage.
    let b: u8 = 250;
    print(b + SZ, " ", b & MASK, " ", HUGE > B, " ", takes_u16(SZ + B));
    let all: u64 = HUGE;
    print(all, " ", peek(SZ));

    // Where nothing gives it a type it is an i64, as a literal is.
    let wide = SZ;
    print(wide * 1000000000000, " ", -NEG);

    // A construct of named constants and literals adapts as a whole.
    let c = b > 100;
    let pick: u8 = if c { SZ } else { 0 };
    print(pick);

    // An argument to an untyped parameter stays literal-like inside the
    // instantiation (§7.7).
    print(add(b, SZ), " ", add(b, B));

    // A cast that a literal would not need is redundant here too.
    let r: u8 = SZ as u8;
    let s: i32 = (B + 1) as i32;
    print(r, " ", s);

    // A typed global and a local let are values of their own type.
    let local = 9;
    print(TYPED + b, " ", local + b);
}
