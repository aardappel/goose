// Explicit type arguments for type variables no parameter mentions (§7.7):
// each distinct list is its own specialization, however the call reaches it.
enum Shape { Circle { r: i64 }, Square { s: i64 } }

// T appears only in the body.
fn size<T>() -> i64 {
    let one: T[1] = [default<T>()];
    bytes_of(one).len
}
// Passed on explicitly, as is, and inside a larger type.
fn wrap<T>(n: i64) -> i64 { size<T>() * n }
fn doubled<T>() -> i64 { wrap<T[2]>(1) }

// T appears only in the result type.
fn zero<T>() -> T { default<T>() }
fn filled<T>(n: i64) -> T[>..] {
    var xs: T[>..] = [];
    for i in 0..n { xs.push(default<T>()); }
    return xs;
}

// A leading explicit list, the rest inferred. Both calls of `both` in main
// bind A to i32 and B to f64, only in a different order.
fn pair_size<T, U>(u: U) -> i64 { size<T>() * 100 + size<U>() }
fn both<A, B>(b: B, a: A) -> i64 { size<A>() * 100 + size<B>() }

// A back edge with the same argument reuses the specialization in progress.
recursive fn countdown<T>(n: i64) -> i64 {
    if n == 0 { return 0; }
    size<T>() + countdown<T>(n - 1)
}

// A nested function's own type variable beside the enclosing one.
fn nested<T>() -> i64 {
    fn inner<U>() -> i64 { size<U>() * 100 + size<T>() }
    inner<u8>() + inner<i64>()
}

// Case functions dispatched with an explicit list.
fn area<T>(c: Shape.Circle) -> i64 { size<T>() }
fn area<T>(s: Shape.Square) -> i64 { size<T>() * 10 }

// A named function value called with an explicit list.
fn apply<F>(n: i64) -> i64 { F<u16>() * n }

fn main() {
    print(size<u8>(), " ", size<f64>(), " ", size<i32>());
    print(wrap<u8>(10), " ", wrap<f64>(10));
    print(doubled<u8>(), " ", doubled<f64>());
    let a: u8 = zero<u8>();
    let b: f64 = zero<f64>();
    print(a, " ", b + 0.5);
    let bytes = filled<u8>(3);
    let floats = filled<f64>(2);
    print(bytes.len, " ", bytes_of(bytes).len, " ", floats.len, " ", bytes_of(floats).len);
    let x = 1.5;
    print(pair_size<u8>(x), " ", pair_size<f64>(x));
    let y: i32 = 7;
    print(both<i32>(x, y), " ", both(x, y));
    print(countdown<i32>(3), " ", countdown<u8>(3));
    print(nested<i32>(), " ", nested<u16>());
    var sh: Shape = Shape.Circle { r: 1 };
    print(area<u8>(sh), " ", area<f64>(sh));
    sh = Shape.Square { s: 2 };
    print(area<u8>(sh), " ", area<f64>(sh));
    print(apply(3, size));
}
