// A generic struct literal infers its type arguments from its initializers
// as written, in the order written, however often it is checked (§7.7): a
// call's argument is checked to pick the overload and again at its
// parameter, by then in declaration order beside the defaults it took.
// Those bind nothing, and resolve in the declaration's environment, which a
// local of the caller's does not hide (§3.2).

let LIMIT: i64 = 7;
struct Box<T> { n: T = 0, a: T, lim: i64 = LIMIT }

fn show(b: Box<f64>) { print(b.n, " ", b.a, " ", b.lim); }

fn main() {
    show(Box { a: 1.5 });
    show(Box { a: 2.5, n: 1 });
    var LIMIT: i64;
    show(Box { a: 3.5 });
    LIMIT = 3;
    print(LIMIT);
}
