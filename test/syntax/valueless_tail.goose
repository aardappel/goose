// A trailing construct that cannot supply a value is a statement, however
// many blocks it sits inside. A function whose return type is inferred
// checks its tail as the value it returns, so the test for "this tail cannot
// be a value" -- an if without a final else, or with a branch that cannot be
// one; a match with an arm that cannot; a loop no break of which carries a
// value; a guard -- has to look through the `block { }` and bare scopes that
// may wrap one. Function-value bodies take their result from their tail the
// same way and ask the same question. The point of the test is that every
// function here compiles; what still produces a value, and gives a function
// its result, is the other half, at the end.

fn bare(n: i64) { if n > 0 { print(n); } }
fn in_scope(n: i64) { { if n > 0 { print(n); } } }
fn in_block(n: i64) { block { if n > 0 { print(n); } } }
fn nested(n: i64) { block { { if n > 0 { print(n); } } } }
fn guard_tail(n: i64) { block { guard n > 0; print(n); } }
fn after_stmts(n: i64) { block { print("first"); if n > 0 { print(n); } } }

// An else-if chain without a final else, ifs with a branch that cannot be a
// value, and a match with an arm that cannot.
fn chain(n: i64) { if n > 1 { print("many"); } else if n > 0 { print("one"); } }
fn then_open(n: i64) { if n > 0 { if n > 1 { print("many"); } } else { print("none"); } }
fn else_open(n: i64) { if n > 1 { print("many"); } else { if n > 0 { print("one"); } } }
fn match_open(n: i64) { match n { 0 => print("none"), _ => if n > 1 { print("many"); } } }

// A loop whose breaks carry no value is a statement too, so a grow-only
// array may shrink inside it, as it may not inside a loop producing a value
// (§5.1). The valued breaks in it leave the `block` and the loop nested in
// it, not this loop.
var trail: i64[>..] = [];
fn drain(n: i64) {
    var left = n;
    loop {
        let step = block { if left > 2 { break 2; } 1 };
        let echo = loop { break left; };
        trail.push(echo);
        print(trail.len, " ", trail[0]);
        trail.clear();
        left -= step;
        if left <= 0 { break; }
    }
}

fn run<F>(n: i64) { for i in n { F(i); } }

// The same shapes as the body of a function value, whose tail is its result.
fn blocks_in_funvals() {
    run(3) { if it > 1 { print(it); } };
    run(3) { block { if it > 1 { print(it); } } };
    run(3) { { if it > 1 { print(it); } } };
    run(3) { block { guard it > 1; print(it); } };
    run(3) { if it > 1 { print(it); } else if it > 0 { print("one"); } };
    run(3) { match it { 0 => print("none"), _ => if it > 1 { print(it); } } };
}

// A block that really does produce a value still does, by its tail or by a
// break, and a valueless tail inside it does not change that. A complete
// if-else chain, a match every arm of which gives a value, and a loop with a
// valued break give an inferred result as they did.
fn valued(c: bool) -> i64 { block { if c { break 1; } 2 } }
fn valued_breaks(c: bool) -> i64 { block { if c { break 1; } break 2; } }
fn valued_after_guard(c: bool) -> i64 { block { guard c else { break 0; } 7 } }
fn chain_value(n: i64) { if n > 1 { "many" } else if n > 0 { "one" } else { "none" } }
fn match_value(n: i64) { match n { 0 => "none", 1 => "one", _ => "many" } }
fn loop_value() { loop { break 5; } }

fn main() {
    bare(1); in_scope(1); in_block(1); nested(1); guard_tail(1); after_stmts(1);
    bare(0); in_scope(0); in_block(0); nested(0); guard_tail(0); after_stmts(0);
    for n in 3 { chain(n); then_open(n); else_open(n); match_open(n); }
    drain(5);
    print(trail.len);
    blocks_in_funvals();
    print(valued(true), " ", valued(false));
    print(valued_breaks(true), " ", valued_breaks(false));
    print(valued_after_guard(true), " ", valued_after_guard(false));
    print(chain_value(0), " ", chain_value(1), " ", chain_value(2));
    print(match_value(0), " ", match_value(1), " ", match_value(2));
    print(loop_value());
}
