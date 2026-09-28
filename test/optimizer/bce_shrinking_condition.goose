// runtime-debug: a grow-only array a loop condition shrinks after testing the index keeps the body's bounds check.
var g: i64[>..] = [];
fn refill(n: i64) -> bool {
    g.clear();
    for i in n { g.push(i * 10); }
    return n > 0;
}
fn main() {
    refill(5);
    var i = 0;
    var total = 0;
    // The index is tested against the length before refill shortens g.
    while i < g.len && refill(g.len - 1) {
        total += g[i]; // bce:keep
        print(total);
        i++;
    }
    print(total);
}
