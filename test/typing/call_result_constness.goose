// A call's result is as writable as what the function returns (§9.5),
// wherever the caller puts it: a callee is checked on its own terms, its
// tail a return whose constness is inferred, so a read-only instantiation
// typechecks when its result is copied into a by-value field, stored in a
// `const` slot, or handed straight to another parameter -- even when that
// slot is the first place the specialization is instantiated from.
import std;

struct User { name: u8[..16] }
struct Named { name: const u8[:] }

// Like std.trim: a parameter generic over constness, passed on through a
// tail call, with the declared result type fit at the tail.
fn inner(s: u8[:]) -> u8[:] { s[1..s.len - 1] }
fn outer(s: u8[:]) -> u8[:] { inner(inner(s)) }
fn count_x(s: u8[:]) -> i64 { var n = 0; for c in s { if c == 'x' { n++; } } n }

fn main() {
    // First instantiations, straight from struct-literal fields.
    let u = User { name: trim(" ada ") };
    let n = Named { name: outer("[[bob]]") };
    print(u.name);
    print(n.name);
    // Straight into another parameter; the argument is a slice expression.
    let age, ok = parse_int(trim("  36 "[1..]));
    print(age);
    print(ok);
    print(count_x(outer("--xx--")));
    // An annotated `const` variable and `const` elements.
    let t: const u8[:] = trim("  tabs  ");
    var views: const u8[:][>..] = [];
    views.push(trim(" one "));
    views.push(outer("((two))"));
    print(t);
    print(views);
    // Given writable storage, the same functions stay writable.
    var a: u8[5] = " cat ";
    let w = trim(a[..]);
    w[0] = 'C';
    print(a);
    var b: u8[7] = "<<dog>>";
    outer(b[..])[1] = 'i';
    print(b);
}
