// runtime-debug: a resize sets the length of the array its receiver named before the count rebound it.
fn main() {
    var a: i64[>..<] = [1];
    var b: i64[>..<] = [];
    var r .= a;
    fn change() -> i64 { r .= b; return 5; }
    r.resize(change(), 7);
    print(a, " ", r.len);
    print(r[4]); // bce:keep
}
