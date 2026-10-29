// error: .range_index takes its abort message as a string literal
fn main() {
    var a: i64[>..] = [1, 2];
    let why = "not a run of a";
    print(a.range_index(a[..], why));
}
