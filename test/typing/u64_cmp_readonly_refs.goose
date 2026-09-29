// A `let` bound to a non-negative value meets a u64 in a comparison (§6.1)
// while nothing can write it (§4.4): a read-only reference, a by-value
// argument and an identity comparison leave it so, and the comparison stays
// a single unsigned one, exact above i64.max.

fn show(r: const i64&) -> i64 { r }
fn twice(x: i64) -> i64 { x * 2 }

fn main() {
    let k = 5;
    let big: u64 = 18446744073709551615;
    let small: u64 = 3;
    var other = 7;
    let c: const i64& = k;
    print(show(k), " ", twice(k), " ", c, " ", k .== other, " ", c .== k);
    var r: const i64& = other;
    r .= k;
    print(r);
    print(small < k, " ", big > k, " ", big == k);
    let j = k;
    print(small < j, " ", big < j);
}
