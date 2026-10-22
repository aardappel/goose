// warning: redundant `as i64`: x is already i64
// warning: redundant `as i64`: an i32 converts to f64 implicitly here
// warning: redundant `as i64`: x is already i64
// A call's casts are judged by resolving it without each (§6.3, §7.1), as
// well where it takes the one overload that converts an integer to a float
// as where it dispatches on a variant (§8.2): there the cast is redundant
// where no overload takes the call without it and it dispatches to the same
// cases. A cast that decides the overload, or a generic case's
// instantiation, stays.
enum Choice { A, B }
fn f(a: f64) -> f64 { a }
fn use(a: i64, c: Choice.A) { print(a, " A"); }
fn use(a: i64, c: Choice.B) { print(a, " B"); }
fn use(a: i32, c: Choice) { print(a, " i32"); }
fn gen<T>(a: T, c: Choice.A) { print(a, " gen A"); }
fn gen<T>(a: T, c: Choice.B) { print(a, " gen B"); }
fn main() {
    let x: i64 = 3;
    let y: i32 = 4;
    print(f(x as i64));
    print(f(y as i64));
    var c: Choice = Choice.A {};
    use(x as i64, c);
    // Without the cast, use(a: i32, c: Choice) takes the call.
    use(y as i64, c);
    c = Choice.B {};
    // Without the cast, the cases are gen<i32>'s.
    gen(y as i64, c);
    use(y, c);
}
