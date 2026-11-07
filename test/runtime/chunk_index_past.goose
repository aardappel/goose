// a.chunk(k, n) has runs 0 to n - 1: k = n is past the last (§9.3).
fn main() {
    var a: i64[>..] = [1, 2, 3, 4, 5];
    for k in 3 { print(a.chunk(k, 3)); }
    let k = a.len - 2;
    print(a.chunk(k, 3));                // Aborts: index 3, of 3 runs.
}
