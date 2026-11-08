// A negative chunk index aborts as one past the last does.
fn main() {
    let a: i64[4] = [1, 2, 3, 4];
    var k = 1;
    print(a.chunk(k, 2));                // [3, 4]
    k -= 2;
    print(a.chunk(k, 2));                // Aborts: index -1, of 2 runs.
}
