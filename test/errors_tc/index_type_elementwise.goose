// error: operator + cannot be applied to Pair and Pair
// Elementwise arithmetic gives a value of its operands' type, so it takes no
// struct with an index-typed field: 3 + 3 is no index of xs, which indexing
// it would trust unchecked (§3.11, §6.1).
struct Pair { a: u32 in xs, b: u32 in xs }
var xs: i64[>..] = [];
fn main() {
    for i in 4 { xs.push(i * 10); }
    let p = Pair { a: 3 as (u32 in xs), b: 3 as (u32 in xs) };
    let q = p + p;
    print(xs[q.a]);
}
