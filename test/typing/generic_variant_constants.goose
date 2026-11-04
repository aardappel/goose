// A payload-less variant of a generic enum, named with its type arguments
// (§3.5, §7.7): `Opt<u8>.None` is a constant of the enum type, as the
// `Plain.None` of a non-generic enum is, wherever a value may stand.
enum Opt<T> { None, Some { v: T } }
enum Plain { None, Some { v: i64 } }

fn none_of<T>() -> Opt<T> { Opt<T>.None }
fn is_none<T>(o: Opt<T>) -> bool { o == Opt<T>.None }
fn take(o: Opt<u8>) -> i64 { match o { None => 0, Some s => s.v } }

fn main() {
    var a = Opt<u8>.None;
    print(a, " ", Plain.None);
    // The enum type, not the variant's: another variant may replace it.
    a = Opt<u8>.Some { 3 };
    print(a, " ", take(a), " ", take(Opt<u8>.None));
    // Where a struct literal may not stand, `{` opens the body.
    if a == Opt<u8>.None { print("none"); } else { print("some"); }
    var n = 0;
    while a != Opt<u8>.None { a = Opt<u8>.None; n++; }
    print(n, " ", a == Opt<u8>.None);
    // A variant literal with an empty body, as before.
    let b: Opt<u8> = Opt<u8>.None {};
    print(b == a);
    let xs = [Opt<i64>.None, Opt<i64>.Some { 1 }, Opt<i64>.None];
    print(xs);
    let two: Opt<i32> = Opt<i32>.Some { 2 };
    print(none_of<f64>(), " ", is_none(none_of<i32>()), " ", is_none(two));
    let nested = Opt<Opt<u8>>.None;
    print(nested, " ", nested == Opt<Opt<u8>>.None);
    // A type a `const for` binds.
    const for <T> in types<u8, f64> { print(Opt<T>.None == none_of<T>()); }
    // Comparisons stay comparisons.
    let x = 1;
    let y = 2;
    print(x < y, " ", x < y == true);
}
