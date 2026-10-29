// runtime-debug: packed prefixes, elements and escaped field addresses retain
// byte alignment, including pointers stored in a variable-size value.
struct Item { name: u8[], quantities: i64[] }
struct Numbers {
    lead: u8[],
    a: i16, b: u16, c: i32, d: u32, e: i64, f: u64, g: f32, h: f64,
    refs: (i64&)[], floats: f64[], limited: i32[..3]
}
struct Fixed { lead: u8, value: i64 }

fn increment(x: i64&) { x += 1; }
fn total(xs: f64[:]) -> f64 {
    var sum = 0.0;
    for x in xs { sum += x; }
    sum
}

fn main() {
    let value = Item { "initial", [1, 2] };
    print(value.name, ",", value.quantities.len);
    var target = 40;
    var numbers = Numbers { "x", -2, 3, -4, 5, -6, 7, 1.5, 2.5,
                            [target, target], [3.5, 4.5], [8, 9] };
    increment(numbers.e);
    increment(numbers.refs[0]);
    numbers.refs[1] += 1;
    numbers.floats[1] = 5.5;
    numbers.limited.push(10);
    assert(numbers.a == -2 && numbers.b == 3 && numbers.c == -4 && numbers.d == 5);
    assert(numbers.e == -5 && numbers.f == 7 && numbers.g == 1.5 && numbers.h == 2.5);
    assert(target == 42 && total(numbers.floats) == 9.0);
    let expected: i32[3] = [8, 9, 10];
    assert(numbers.limited == expected);
    var fixed = Fixed { 0, 10 };
    increment(fixed.value);
    assert(fixed.value == 11);
}
