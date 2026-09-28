// runtime-debug: a signed type's minimum written as a literal is a constant,
// never a negation left to run time. -9223372036854775808 negates the u64
// literal 2^63, which is exactly i64.min (§6.2), where a debug build's
// checked negation of the literal would abort as an overflow.

struct Limits { lo: i64 = -9223372036854775808, hi: i64 = 9223372036854775807 }
struct Packed { v: varint }
struct Narrow { a: i8 = -128, b: i16 = -32768, c: i32 = -2147483648 }

let MIN = -9223372036854775808;
var mins: i64[3] = [-9223372036854775808, -0x8000000000000000, -(9223372036854775808)];

fn id(x: i64) -> i64 { x }
fn low(x: i64 = -9223372036854775808) -> i64 { x }
fn id8(x: i8) -> i8 { x }
fn id16(x: i16) -> i16 { x }
fn id32(x: i32) -> i32 { x }

fn main() {
    print(-9223372036854775808);
    print(id(-9223372036854775808));
    print(low());
    let y = -9223372036854775808;
    print(y);
    print(MIN);
    print(mins[0], " ", mins[1] == mins[0], " ", mins[2] == mins[0]);
    print(Limits {}.lo);
    print(Packed { -9223372036854775808 }.v);
    print(-9223372036854775808 + 1);
    // Against the minimum computed at run time, and the parser's own
    // negation in a match pattern.
    for k in 2 {
        let v = k - 9223372036854775807 - 1;
        print(v == -9223372036854775808);
        match v {
            -9223372036854775808 => print("min"),
            _ => print("above"),
        }
    }
    // The narrower types' minimums, each at its own type.
    let a: i8 = -128;
    let b: i16 = -32768;
    let c: i32 = -2147483648;
    print(a, " ", b, " ", c);
    print(id8(-128), " ", id16(-32768), " ", id32(-2147483648));
    let n = Narrow {};
    print(n.a, " ", n.b, " ", n.c);
    // `as` binds tighter than `-` (Appendix D): parenthesized, the cast
    // converts -128 rather than 128, which i8 does not hold.
    print((-128) as i8, " ", (-2147483648) as i32);
}
