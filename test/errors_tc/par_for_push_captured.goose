// error: cannot .push xs in a par for: xs is a variable of the enclosing function
// R1 (§4.3): no push, resize or shrink of what the body sees from outside it.
fn main() {
    var xs: i64[>..] = [1, 2, 3, 4];
    par for k in 4 { xs.push(k); }
    print(xs);
}
