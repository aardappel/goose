// error: .index_of takes its abort message as a string literal
fn main() {
    var xs: i64[>..] = [1, 2];
    let r: i64& = xs[1];
    let msg = "not an element";
    print(xs.index_of(r, msg));
}
