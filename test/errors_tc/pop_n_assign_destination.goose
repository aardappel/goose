// error: cannot pop_n g: g[0], which the assignment writes after it, may be in g
// The destination of an assignment is evaluated before a pop_n in its value
// and written after it (§5.1).
fn main() {
    var g: i64[>..] = [1, 2];
    g[0] = { g.pop_n(1); 5 };
    print(g);
}
