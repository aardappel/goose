// A literal argument to a generic or untyped parameter stays a literal
// inside the specialization (§7.7): it adapts wherever the parameter is
// used, so `push_n(flags, 1, n)` pushes a u8 into a u8 array.
import std;

fn first_or(xs: T[:], d: T) -> T { if xs.len > 0 { xs[0] } else { d } }
fn twice(v) { v + v }
fn fill<A, T>(xs: A&, v: T, n: i64) { for i in n { xs.push(v); } }
// A recursive call passes the committed type, so this terminates.
recursive fn count<T>(v: T, n: i64) -> i64 { if n == 0 { return v; } return count(v + 1, n - 1); }

fn main() {
    var flags: u8[>..] = [];
    push_n(flags, 1, 3);
    flags.push_n(0, 2);
    var fs: f32[>..] = [];
    fill(fs, 1.5, 2);
    var small: i16[..4] = [];
    fill(small, -7, 2);
    let e: u8[:] = flags[..];
    print(flags.len, " ", flags[0], flags[3], " ", fs, " ", small, " ", first_or(e, 9), " ", first_or(fs, 2.5));
    print(count(0, 4), " ", twice(21), " ", max(flags[0], 7), " ", twice(1.25));
}
