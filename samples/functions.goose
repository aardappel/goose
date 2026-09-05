// Functions: generics without declaring them, overloading, UFCS, function
// values that compile to plain loops, nested functions that see their
// enclosing locals, and a `return` from inside a block that leaves the
// whole enclosing function. Everything here is resolved at compile time;
// there are no function pointers and nothing is boxed.
//
// Build and run (from this directory):
//   goose -o functions.c functions.goose && cl functions.c && functions

import std;

// An untyped parameter is a generic one: the function is compiled once per
// argument type it is called with.
fn twice(x) { x + x }

// Explicit type parameters say that two arguments must agree.
fn span<T>(a: T, b: T) -> T { if a > b { a - b } else { b - a } }

// Overloads resolve on the argument types; a slice parameter takes any
// array kind, or a sub-range of one.
fn describe(x: i64) -> u8[>..] { str("int ", x) }
fn describe(x: f64) -> u8[>..] { str("float ", x) }
fn describe(xs: i64[:]) -> u8[>..] { str(xs.len, " ints, first ", xs[0]) }

// A higher-order function: F is a function value, a compile-time parameter.
// Calling F(...) inlines the block; this compiles to the loop it looks like.
fn each_pair<T, F>(xs: T[:]) {
    for i in 0..xs.len - 1 { F(xs[i], xs[i + 1]); }
}

// A HOF may return what its block produces, without saying what that is:
// the block is typechecked at each call, so U is whatever F returns.
fn best_by<T, F>(xs: T[:]) -> T {
    var b = xs[0];
    var bk = F(b);
    for x in xs[1..] {
        let k = F(x);
        if k > bk { b = x; bk = k; }
    }
    return b;
}

// A HOF that stops early: the block may `return` from the *caller*, which
// gives a hand-written iterator the same power as a for loop.
fn each_step<F>(lo: i64, hi: i64, step: i64) {
    var i = lo;
    while i < hi { F(i); i += step; }
}

struct Employee { name: u8[..12], dept: u8[..8], salary: i32 }

// The function that `return`s through each_step.
fn first_multiple_of_7(lo: i64, hi: i64) -> i64 {
    each_step(lo, hi, 3) { if it % 7 == 0 { return it; } };
    return -1;
}

fn main() {
    print(twice(21), " ", twice(1.5), " ", span(3, 8), " ", span(2.5, 1.0));
    var xs: i64[>..] = [4, 8, 15, 16, 23, 42];
    // UFCS: x.f(a) is f(x, a), for any function.
    print(describe(7), "; ", 2.5.describe(), "; ", xs.describe(), "; ", describe(xs[2..]));
    print(twice(3).describe());

    // Blocks: the implicit `it`, or named parameters. No allocation, no
    // indirect call: each one is compiled into its caller.
    var gaps: i64[>..] = [];
    each_pair(xs) { a, b => gaps.push(b - a); };   // writes an enclosing local
    print("gaps ", gaps);
    print("biggest gap ", max(gaps), " at ", max_by(gaps) { it });
    print(first_multiple_of_7(10, 40), " ", first_multiple_of_7(10, 12));

    // The standard library is written this way too.
    let evens = xs.filter() { it % 2 == 0 };
    let squares = xs.map() { it * it };
    let total = fold(xs, 0) { acc, x => acc + x };
    print(evens, " ", squares, " ", total, " ", xs.any() { it > 40 }, " ", xs.count() { it < 10 });

    var staff: Employee[>..] = [];
    staff.push(Employee { name: "ada", dept: "eng", salary: 120 });
    staff.push(Employee { name: "grace", dept: "eng", salary: 135 });
    staff.push(Employee { name: "linus", dept: "ops", salary: 90 });
    staff.push(Employee { name: "ken", dept: "ops", salary: 95 });
    let top = best_by(staff) { it.salary };
    print("top earner ", top.name);
    sort(staff) { a, b => a.salary > b.salary };
    for e in staff { print("  ", e.name, " ", e.dept, " ", e.salary); }

    // A nested function reads and writes the locals around it, like a block,
    // and can be called by name more than once.
    var budget = 0;
    var seen = 0;
    fn spend(e: Employee&) {
        budget += e.salary;
        seen++;
    }
    for &e in staff { if e.dept[..] == "eng" { spend(e); } }
    print("eng budget ", budget, " over ", seen, " people");

    // A named function as a value, where a block would be too long.
    fn by_name(a: Employee, b: Employee) -> bool { compare(a.name[..], b.name[..]) < 0 }
    sort(staff, by_name);
    for e in staff { print("  ", e.name); }
}
