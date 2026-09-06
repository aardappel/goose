// A literal argument to a generic or untyped parameter stays a literal
// inside the specialization (§7.7): it adapts wherever the parameter is
// used, so `push_n(flags, 1, n)` pushes a u8 into a u8 array. The
// specialization does not depend on the literal's value, so every call
// with a fitting literal shares it.
import std;

fn first_or(xs: T[:], d: T) -> T { if xs.len > 0 { xs[0] } else { d } }
fn twice(v) { v + v }
fn fill<A, T>(xs: A&, v: T, n: i64) { for i in n { xs.push(v); } }
// Passed on as a literal: the callee's uses constrain this call's literal.
fn fill2<A>(xs: A&, v) { fill(xs, v, 2); }
// Handed to a block: the block's use constrains it too.
fn each_n<F>(v, n: i64) { for i in n { F(v); } }
// The other branch types the literal, as it would a constant.
fn pick(c: bool, x: i16, v) -> i16 { if c { x } else { v } }
// A recursive call passes the committed type.
recursive fn count<T>(v: T, n: i64) -> i64 { if n == 0 { return v; } return count(v + 1, n - 1); }

fn main() {
    var flags: u8[>..] = [];
    push_n(flags, 1, 3);
    flags.push_n(0, 2);
    push_n(flags, 255, 1);
    var fs: f32[>..] = [];
    fill(fs, 1.5, 2);
    var small: i16[..4] = [];
    fill(small, -7, 2);
    fill2(small, 300);
    each_n(9, 2) { flags.push(it); };
    let e: u8[:] = flags[..];
    print(flags.len, " ", flags[0], flags[3], flags[5], " ", flags[6], " ", fs, " ", small, " ",
          first_or(e, 9), " ", first_or(fs, 2.5));
    print(count(0, 4), " ", twice(21), " ", max(flags[0], 7), " ", twice(1.25), " ",
          pick(false, 1, -3), " ", pick(true, 1, 5));
}
