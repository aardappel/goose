// error: cannot use r in the value assigned to x: r may refer to x
fn twice(v: i64[>..]&) -> i64[>..] {
    var out: i64[>..] = [];
    for e in v { out.push(e); }
    for e in v { out.push(e); }
    return out;
}
fn main() {
    var x: i64[>..] = [1, 2, 3];
    let r = x;
    x = twice(r);
    print(x);
}
