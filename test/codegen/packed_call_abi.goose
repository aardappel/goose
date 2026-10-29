// runtime-debug: packed members crossing an eight-byte boundary survive
// by-value arguments and returns. Recursion keeps a C call boundary.
struct Box<T> { lead: u8, value: T }
enum Choice<T> { Value { value: T }, Empty }

recursive fn relay<T>(value: T, depth: i64) -> T {
    if depth == 0 { return value; }
    let result = relay(value, depth - 1);
    assert(result == value);
    return result;
}

fn main() {
    let integer = 0x123456789abcdef;
    let box = Box<i64> { 7, integer };
    assert(relay(box, 20) == box);
    let choice: Choice<i64> = Choice<i64>.Value { integer };
    assert(relay(choice, 20) == choice);
    let real = Box<f64> { 8, 1234.5 };
    assert(relay(real, 20) == real);
    var target = integer;
    let pointer = Box<i64&> { 9, target };
    assert(relay(pointer, 20).value == integer);
    let array = Box<i64[1]> { 10, [integer] };
    assert(relay(array, 20) == array);
    let limited: i64[..1] = [integer];
    assert(relay(limited, 20) == limited);
    print("packed calls ok");
}
