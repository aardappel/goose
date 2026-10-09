// A function name with overloads is a function value too (§7.6): it passes
// the whole overload set, and each call of the value resolves among them as
// a call by that name would.
import std;

fn each_sorted<F>(xs: i64[:]) { F(xs); }

fn bump(x: i64) -> i64 { x + 1 }
fn bump(x: f64) -> f64 { x + 0.5 }
fn bump(s: u8[:]) -> i64 { s.len }

fn twice<F>(x: i64) -> i64 { F(F(x)) }

// One value, called with arguments that pick different overloads.
fn show<F>(a: i64, b: f64, s: u8[:]) { print(F(a), " ", F(b), " ", F(s)); }

// Passed on through another function value parameter.
fn relay<F>(a: i64, b: f64, s: u8[:]) { show(a, b, s, F); }

fn main() {
    var xs = [3, 1, 2];
    // std's sort<T>(xs) and sort<T, F>(xs): one argument and no function
    // value pick the first.
    each_sorted(xs, sort);
    print(xs);
    each_sorted(xs, reverse);
    print(xs);
    print(twice(1, bump));
    show(1, 2.0, "abc", bump);
    relay(10, 0.25, "", bump);
    print(map(xs, bump));
    print(fold(xs, 0, max));
}
