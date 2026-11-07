// error: values is or may point into storage partitioned by this par for
// R2 (§4.4): a function checked for one par for and reused in another is
// judged by the second too: peek reads the global values, which dst may be.
var values: i64[>..] = [];

fn peek() -> i64 { return values[0]; }

fn fill(dst: i64[:]) {
    var seen = 0;
    par for k in 4 { seen += peek(); }
    par for k in dst.len { dst[k] = peek(); }
    print(seen);
}

fn main() {
    for i in 10 { values.push(i + 1); }
    fill(values);
    print(values);
}
