// a.chunk(k, n) cuts a into n runs: a count of zero has no run to give, and
// aborts rather than divide by it (§9.3).
fn part(a: i64[:], k: i64, n: i64) -> i64[:] { a.chunk(k, n) }

fn main() {
    var a: i64[4] = [1, 2, 3, 4];
    print(part(a, 0, 1));                // [1, 2, 3, 4]
    print(part(a, 0, 0));                // Aborts.
}
