// error: cannot resize c: the for loop at
// error: iterates tail(c), which may refer into c
// A slice a call returns is read once, like any sequence that is not a
// variable, and walked for the whole loop (§5.2, §6.5).
fn tail(c: i64[>..<]&) -> i64[:] { c[1..] }
fn main() {
    var c: i64[>..<] = [1, 2, 3, 4];
    for x in tail(c) {
        c.resize(1);
        c.push(9);
        print(x);
    }
}
