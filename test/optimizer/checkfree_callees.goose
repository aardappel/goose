// checkfree (§7.14) judges what runs inside a checkfree function, called or
// inlined, without that being declared checkfree: helpers, a block written in
// a function that is not checkfree, a format overload a print reaches, and
// std's comparator forms with a comparator that holds no check. Its arguments
// are its caller's code, so a check kept in them is the caller's.
import std;

fn total(xs: i32[:]) -> i64 {
    var t = 0;
    for i in xs.len { t += xs[i]; }
    return t;
}
fn twice(xs: i32[:]) -> i64 { total(xs) * 2 }

checkfree fn each<F>(xs: i32[:]) { for x in xs { F(x); } }

struct Pt { x: i64, y: i64 }
fn format(out: u8[>..]&, p: Pt&) { format(out, "(", p.x, ", ", p.y, ")"); }

checkfree fn report(xs: i32[:], p: Pt&) -> i64 {
    print(p);
    let lo = min(xs) { a, b => a < b } + max(xs) { a, b => b < a };
    return twice(xs) + lo + min_by(xs) { -it } + max_by(xs) { -it };
}

checkfree fn sq(x: i64) -> i64 { x * x }
checkfree inline fn sq_inline(x: i64) -> i64 { x * x }

fn main() {
    var a: i32[>..] = [];
    a.push(4);
    a.push(1);
    a.push(7);
    var t = 0;
    each(a) { x => t += x; };
    print(t);
    let p = Pt { 1, 2 };
    print(report(a, p));
    let i = a[1] - 1;   // 0, which nothing shows is in range
    print(sq(a[i]), " ", sq(a[i + 2]), " ", sq_inline(a[i + 1]));
}
