export fn hello_world() -> const u8[:] {
    return "Hello, world!"
}

export "c_add" fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

// A slice parameter: the wrapper takes the C struct and passes its words on.
export "c_weigh" fn weigh(xs: const i64[:], w: i64) -> i64 {
    var t = 0;
    for x, i in xs { t += x * (i + w); }
    return t;
}

// The wrapper, called back through its C symbol.
extern "c_weigh" fn c_weigh(xs: const i64[:], w: i64) -> i64;

fn main() {
    print(hello_world(), " ", add(20, 22));
    let xs: i64[4] = [3, 1, 4, 1];
    print(weigh(xs, 1), " ", c_weigh(xs, 1), " ", c_weigh(xs[1..3], 0));
}
