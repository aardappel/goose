// Control expressions retain grouping as operands, receivers and scrutinees,
// as do literal receivers and the returns and breaks that end match arms.
struct Box { value: i64 }
enum Choice { One { value: i64 }, None }
fn square(value: i64) -> i64 { value * value }
fn apply<F: fn(i64)>(value: i64) { F(value) }
fn early(x: i64) -> i64 {
    match x {
        1 => 6,
        _ => return 5
    }
}
fn double(x: i64) -> i64 { x * 2 }
fn skip(x: i64) {
    match x {
        1 => print(1),
        _ => return
    }
    print(2);
}
fn leave(x: i64) -> i64 {
    var n = 0;
    loop {
        n += 1;
        match n {
            1 => {},
            _ => break return x + n
        }
    }
    return 0
}

fn main() {
    print(1 + (if true { 2 } else { 3 }));
    print((if true { 2 } else { 3 }) + 4);
    print(-(match 1 { 1 => 5, _ => 6 }));
    print((loop { break 7; }) + (block { break 8; }));
    print(({ 9 }) as u8);
    print(({ [10, 11] })[1]);
    print((block { break [12, 13]; })[..]);
    print((if true { Box { 14 } } else { Box { 15 } }).value);
    print((-3).square(), " ", (-3) as! u8);
    print((apply(15) { it + 1 }) + 1);
    match (block { let choice: Choice = Choice.One { 18 }; break choice; }) {
        One v => print(v.value), None => print(0)
    }
    for i in (block { break 19; })..(block { break 21; }) { print(i); }
    if (Box { 22 }).value == 22 { print(22); }
    print(early(1), " ", early(2), " ", (0x10).double());
    var k = 0;
    loop {
        k += 1;
        match k {
            1 => {},
            _ => break
        }
    }
    print(k);
    skip(1);
    skip(3);
    print(leave(5));
}
