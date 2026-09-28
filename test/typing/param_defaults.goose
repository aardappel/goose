// dump-runtime: parameter defaults (§7.1). A call leaves out trailing
// parameters that declare a default, and each gets its default evaluated at
// that call, after the written arguments, as a value of the parameter's type
// there. A default names what a top-level declaration would, in the
// declaration's namespace: never the caller's locals, whatever their names.

struct Opts { width: i64 = 8, fill: u8 = '.' }
struct Point { x: i64, y: i64 }
enum Mode { Fill, Line, Dots }

let BASE = 10;
let TABLE: i64[4] = [5, 6, 7, 8];
let fmt::BASE = 16;
let ORIGIN = 3;
var ticks = 0;
var counter = 0;
var log: i64[>..] = [];

fn tick() -> i64 { ticks += 1; ticks }

// Literals, and a float literal for an f32.
fn weigh(a: i32, b: i32 = 0, c: f32 = 1.0) -> f32 { (a + b) as f32 * c }

// A struct literal, one with field defaults of its own, a string literal
// for a read-only slice, a whole array for one, a negative constant, a
// variant constant, and a `var` parameter's starting value.
fn at(p: Point = Point { x: ORIGIN, y: 4 }) -> i64 { p.x * 10 + p.y }
fn fmt::render(v: i64, base: i64 = BASE, o: Opts = Opts { }) -> i64 { v * base + o.width }
fn greet(name: const u8[:] = "world") { print("hello ", name); }
fn total(xs: const i64[:] = TABLE) -> i64 { var t = 0; for x in xs { t += x; } t }
fn neg(a: i64 = -1) -> i64 { a }
fn opt(a: i64, o: i64? = null) -> i64 { if o { a + o } else { a } }
fn draw(n: i64, mode: Mode = Mode.Line) -> i64 {
    match mode { Fill => n, Line => n * 2, Dots => n * 3 }
}
fn count(var a: i64 = 3) -> i64 { a += 1; a }

// Globals: read, written through a reference parameter, grown through one,
// and a call evaluated anew at each call leaving it out.
fn addto(v: i64, to: i64 = BASE) -> i64 { v + to }
fn bump(r: i64& = counter, by: i64 = 1) { r += by; }
fn collect(xs: i64[>..]& = log, v: i64 = 7) { xs.push(v); }
fn pair(a: i64, b: i64 = tick()) { print(a, " ", b); }

// A resizable value constructed in the parameter's slot.
fn fresh(xs: i64[>..] = [], n: i64 = 3) -> i64 {
    var ys = xs;
    for i in n { ys.push(i); }
    ys.len
}

// Generics: the default takes the type the arguments bind, and decides none
// itself, so a type only it would fix is given explicitly.
fn scale<T>(x: T, by: T = 2) -> T { x * by }
fn fill_all<T>(xs: T[:], v: T = default<T>()) { for &x in xs { x = v; } }
fn widened<T>(a: i64, b: T = 5) -> i64 { a + b as i64 }

// A block default with a local of its own, and a default calling a
// function that has a default itself.
fn blocky(a: i64, b: i64 = { let t = BASE; t * 2 }) -> i64 { a + b }
fn twice(a: i64, b: i64 = 2) -> i64 { a * b }
fn nested_default(a: i64 = twice(3)) -> i64 { a + 1 }
// A default may call its own function, giving that call every argument.
fn chained(a: i64 = 1, b: i64 = chained(5, 6)) -> i64 { a + b }

recursive fn countdown(n: i64, acc: i64 = 0) -> i64 {
    if n == 0 { return acc; }
    return countdown(n - 1, acc + n);
}
recursive fn depth(n: i64, d: i64 = 1) -> i64 {
    if n == 0 { return d; }
    return depth(n - 1) + d;
}
// A tail call taking a default, which becomes a loop at -O1 and -O2 (the
// leading local keeps base-case inlining from taking it first).
recursive fn leading_digit(n: i64, base: i64 = 10) -> i64 {
    let q = n / base;
    if q == 0 { return n; }
    return leading_digit(q);
}

// A named function passed as a function value keeps its defaults; a
// function value can follow fewer arguments than the parameters.
fn apply<F>(x: i64) -> i64 { F(x) }
fn each_step<F>(n: i64, step: i64 = 1) {
    var i = 0;
    while i < n { F(i); i += step; }
}
fn show(i: i64) { print("show ", i); }

// An extern: the C function gets every argument.
extern "fmod" fn fmod_by(a: f64, b: f64 = 3.0) -> f64;

// Nested functions: a default names the global, not the local of the same
// name the enclosing function declares after it.
fn outer(k: i64) -> i64 {
    fn helper(x: i64, y: i64 = BASE) -> i64 { x + y + k }
    let BASE = 1000;
    return helper(1) + helper(2, BASE);
}
// It sees the type parameters around it.
fn summed<T>(x: T) -> T {
    fn inner(a: T, b: T = default<T>()) -> T { a + b }
    inner(x) + inner(x, x)
}

let early = twice(4);

fn main() {
    print(weigh(1), " ", weigh(1, 2), " ", weigh(1, 2, 0.5));
    print(at(), " ", at(Point { x: 1, y: 2 }));
    print(fmt::render(1), " ", fmt::render(1, 2), " ", fmt::render(1, 2, Opts { width: 3 }));
    greet();
    greet("goose");
    print(total(), " ", total(TABLE[2..]), " ", neg());
    print(draw(1), " ", draw(1, Mode.Dots), " ", count(), " ", count(10));
    var five = 5;
    print(opt(1), " ", opt(1, five));
    // Callers' locals named like what the defaults name change nothing.
    let BASE = 99;
    let counter = -50;
    print(addto(1), " ", fmt::render(BASE), " ", BASE);
    bump();
    bump();
    bump(::counter, 10);
    var mine = 5;
    bump(mine);
    print(::counter, " ", mine, " ", counter);
    collect();
    var other: i64[>..] = [];
    collect(other, 9);
    print(log, " ", other);
    pair(tick());
    pair(tick(), tick());
    print(fresh(), " ", fresh([1, 2], 1));
    let small: u8 = 3;
    print(scale(21), " ", scale(small));
    var arr = [1, 2, 3];
    fill_all(arr[..]);
    print(arr);
    fill_all(arr[..], 4);
    print(arr);
    print(widened<i32>(1), " ", widened(1, 2));
    print(blocky(1), " ", nested_default(), " ", chained());
    print(countdown(4), " ", depth(3), " ", leading_digit(4711), " ", leading_digit(255, 16));
    print(apply(6, twice), " ", twice(twice(3)));
    each_step(5) { print(it) }
    each_step(5, 2) { print(it) }
    each_step(2, show);
    each_step(4, 3, show);
    print(fmod_by(10.0), " ", fmod_by(10.0, 4.0));
    print(outer(100), " ", summed(5), " ", summed(2.5));
    // Member-call syntax: the receiver is the first argument.
    print(5.addto(), " ", 5.addto(1), " ", 7.twice());
    // Checked again on each pass over a loop body.
    for i in 3 {
        bump();
        print(twice(i));
    }
    print(::counter, " ", early);
}
