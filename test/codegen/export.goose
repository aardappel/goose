// The exports as Goose calls them. Their C wrappers are called the way C
// calls them by the C host test/codegen/export_host.c (run_tests.py's
// host_programs task), which links this program's C.
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

// A struct the program passes by pointer and returns through an out-pointer
// (CodeGen::PtrAbi): the wrapper takes and returns it by C value.
struct Wide { a: i64 = 0, b: i64 = 0, rest: i64[40] = [0; 40] }
export "c_widen" fn widen(var w: Wide, k: i64) -> Wide {
    w.b = w.a + k;
    w
}

// A read-only one, which Goose callers may lend a variable of theirs for.
export "c_peek_wide" fn peek_wide(w: Wide) -> i64 { w.a * 10 + w.b }

fn main() {
    print(hello_world(), " ", add(20, 22));
    let xs: i64[4] = [3, 1, 4, 1];
    print(weigh(xs, 1), " ", weigh(xs[1..3], 0));
    let v = widen(Wide { a: 6, .. }, 3);
    print(v.a, " ", v.b);
    var pw = Wide { a: 3, b: 4, .. };
    print(peek_wide(pw), " ", pw.a);
}
