// runtime-debug: a constant shifted by a count that is no constant takes
// the integer type its destination or other operand has, as the constant
// would (§6.1): a u64 bit set, a u8 field, a parameter, a construct's
// branch, a block's value, a compound assignment, a typed loop count. So
// does integer arithmetic over such shifts and constants, computed at that
// type throughout, which a debug build's overflow checks would tell. Where
// nothing gives it a type it is an i64, so 1 << 63 is i64.min there and
// 2^63 as a u64.

struct Bits { b: u64[2] }
struct Small { m: u8 }
fn same<T>(a: T, b: T) -> T { a | b }
fn bit(k: i64) -> u64 { 1 << k }

fn main() {
    var k = 60;
    k += 3;
    var t = Bits { b: [0, 0] };
    let item = 70;
    t.b[item / 64] = t.b[item / 64] | (1 << (item % 64));
    print(t.b[1]);

    var m: u64 = 0;
    m |= 1 << k;
    print(m, " ", m == (1 << k), " ", bit(k), " ", same(1 << (k - 60), m) == m + 8);

    let sm = Small { m: 1 << (k - 60) };
    let c = k > 1;
    let pick: u64 = if c { 1 << k } else { 0 };
    print(sm.m, " ", pick, " ", m | (block { (1 << k) - 1 }));

    let mask: u64 = ~((1 << k) - 1);
    let low: u64 = (1 << k) - 1;
    let x32: u32 = 3;
    print(mask, " ", low, " ", x32 + (1 << (k - 32)));

    for i: u8 in 1 << (k - 61) { print(i); }

    let f: f32 = 1.5 * (1 << (k - 60));
    let wide = 1 << k;
    print(f, " ", wide);
}
