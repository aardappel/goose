// An array literal whose elements are all integer constants, or all floats
// of literals, meets a parameter as its elements would (§3.1, §6.3, §7.1):
// at an array or slice parameter of its shape with other integer or float
// elements they fit, it is a value of that type, ranking as a coercion.
fn color(c: u8[4]) -> i64 {
    let r: i64 = c[0];
    r * 1000 + c[3]
}
fn bytes(s: const u8[:]) -> i64 {
    var t = 0;
    for b in s { t += b; }
    t
}
fn grow(xs: u8[]) -> i64 { xs.len }
fn grid(g: i16[2][2]) -> i16 { g[0][0] + g[1][1] }
fn halves(a: f32[2]) -> f32 { a[0] + a[1] }
fn reals(a: f64[3]) -> f64 { a[0] + a[1] + a[2] }
fn pick(c: u8[2]) -> i64 { 8 }
fn pick(c: i64[2]) -> i64 { 64 }
fn first<T>(a: T[4]) -> T { a[0] }
fn pair<T>(a: T[2], b: T) -> T[2] { [a[1], b] }
fn shade(c: u8[4] = [10, 20, 30, 40]) -> i64 { c[3] }

fn main() {
    print(color([255, 0, 0, 7]));
    print(bytes([1, 2, 250]));
    print(grow([1, 2, 3]));
    print(grid([[1, -2], [3, -4]]));
    print(halves([0.5, 2]));
    print(reals([1, 2, 3]));
    print(color([9; 4]));
    // An exact match wins over the coercion, as a scalar literal's does.
    print(pick([1, 2]));
    let small: u8[2] = [1, 2];
    print(pick(small));
    // Alone a literal binds T to its own type; beside a typed argument
    // it adapts to the type that gives T.
    let big = first([300, 2, 3, 4]);
    print(big);
    let b: u8 = 200;
    let p: u8[2] = pair([1, 2], b);
    let p0: i64 = p[0];
    print(p0 + p[1]);
    print(shade());
    // Constructs whose branches are all such literals of one type adapt as one.
    let c = big > 0;
    print(color(if c { [1, 2, 3, 4] } else { [5, 6, 7, 8] }));
    print(color({ [4, 3, 2, 1] }));
}
