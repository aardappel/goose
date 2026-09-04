// Using a pending array where its element type is needed, before anything
// determines it (§4.2).
fn total(xs: i64[:]) -> i64 { var s = 0; for x in xs { s += x; } s }
fn main() {
    var xs = [];
    print(total(xs));
    xs.push(1);
}
