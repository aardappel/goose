// error: field initializers must follow declaration order: a comes before b
// A conversion into an index type checks the value against the array, in
// every build, whatever the widths: a's check would abort before b's, which
// the source puts first (§3.11, §4.2).
struct Pair { a: u32 in xs, b: u32 in ys }
var xs: i64[>..] = [];
var ys: i64[>..] = [];
fn main() {
    let n: u32 = 7;
    let m: u32 = 9;
    let p = Pair { b: m as (u32 in ys), a: n as (u32 in xs) };
    print(p.a, " ", p.b);
}
