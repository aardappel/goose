// Inherited function-valued parameters (§7.6): declared among the generic
// parameters, used in the body like any function-valued parameter, but drawn
// by name at each call site from the scope the call is written in. No
// argument carries one; an explicit type argument binds none; a variable of
// the name stands for none (the call site's own resolution of a call by that
// name applies). Each site draws its own value, so the same call makes
// distinct specializations; a set of overloads is drawn whole, and each call
// of the value resolves among them as a call by that name would.
fn foo<inherited bar: fn()>() { bar(); }

fn bar() { print("foobar"); }

// A regular function value beside an inherited one. F arrives as an
// argument; op is drawn.
fn once<F: fn(i64), inherited op: fn(i64)>(x: i64) { print(op(F(x))); }

fn double(x: i64) -> i64 { return 2 * x; }
fn op(x: i64) -> i64 { return x * 3; }
fn add1(x: i64) -> i64 { return x + 1; }

// The same name may be a set of overloads, drawn whole.
fn disp<inherited twice: fn(i64)>(x: i64) { print(twice(x)); }
fn lens<inherited twice: fn(u8[:])>(s: u8[:]) { print(twice(s)); }
fn twice(x: i64) -> i64 { return 2 * x; }
fn twice(s: u8[:]) -> i64 { return s.len; }

// A nested function is drawn with its free variables, as passing it would.
fn outer() {
    var msg = "from nested";
    fn bar() { print(msg); }
    foo();
    msg = "changed";
    foo();
}

// A passed-on value: the middle layer's own function-value parameter of the
// name, which the inherited parameter finds in scope at that call.
fn middle<F: fn(i64), op: fn(i64)>(x: i64) { once(x, F); }

// Two regular values, one drawn, one a trailing block (§7.6).
fn trio<F: fn(i64), G: fn(i64), inherited cb: fn(i64)>(x: i64) {
    cb(F(x));
    G(x);
}
fn cb(x: i64) { print("cb ", x); }

// A default parameter is an ordinary value beside one.
fn scaled<inherited op: fn(i64)>(x: i64, by: i64 = 3) { print(op(x * by)); }

fn main() {
    foo();
    {
        fn op(x: i64) -> i64 { return 10 * x; }
        once(5, double); // the block's op drawn: op(double(5)) = 100
    }
    once(10, add1); // add1 needs its own scope; the global op drawn: 33
    disp(5);        // twice(i64)(5) is 10
    lens("hello");  // twice(u8[:])("hello") is 5
    outer();        // two nested cb draws, free variables followed
    middle(9, add1, double); // once(9): F=add1, op=middle's double: 20
    trio(5, double) { v => print("block G ", v); }
    scaled(2); // the global op drawn: op(2 * 3) = 18
    {
        fn bar() { print("inner"); }
        foo();
    }
    foo();
}
