// A trailing construct that produces nothing on any path is a statement,
// however many blocks it sits inside. A function whose return type is
// inferred checks its tail as the value it returns, so the test for "this
// tail cannot be a value" -- an else-less if, a guard -- has to look through
// the `block { }` and bare scopes that may wrap one. Function-value bodies
// take their result from their tail the same way and ask the same question.
// The point of the test is that every function here compiles; a `block`
// whose value some caller does want is the other half, at the end.

fn bare(n: i64) { if n > 0 { print(n); } }
fn in_scope(n: i64) { { if n > 0 { print(n); } } }
fn in_block(n: i64) { block { if n > 0 { print(n); } } }
fn nested(n: i64) { block { { if n > 0 { print(n); } } } }
fn guard_tail(n: i64) { block { guard n > 0; print(n); } }
fn after_stmts(n: i64) { block { print("first"); if n > 0 { print(n); } } }

fn run<F>(n: i64) { for i in n { F(i); } }

// The same shapes as the body of a function value, whose tail is its result.
fn blocks_in_funvals() {
    run(3) { if it > 1 { print(it); } };
    run(3) { block { if it > 1 { print(it); } } };
    run(3) { { if it > 1 { print(it); } } };
    run(3) { block { guard it > 1; print(it); } };
}

// A block that really does produce a value still does, by its tail or by a
// break, and a valueless tail inside it does not change that.
fn valued(c: bool) -> i64 { block { if c { break 1; } 2 } }
fn valued_breaks(c: bool) -> i64 { block { if c { break 1; } break 2; } }
fn valued_after_guard(c: bool) -> i64 { block { guard c else { break 0; } 7 } }

fn main() {
    bare(1); in_scope(1); in_block(1); nested(1); guard_tail(1); after_stmts(1);
    bare(0); in_scope(0); in_block(0); nested(0); guard_tail(0); after_stmts(0);
    blocks_in_funvals();
    print(valued(true), " ", valued(false));
    print(valued_breaks(true), " ", valued_breaks(false));
    print(valued_after_guard(true), " ", valued_after_guard(false));
}
