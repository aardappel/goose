// runtime-debug: a push grows the array its receiver named before the argument rebound it.
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [];
    var r .= a;
    fn change() -> i64 { r .= b; return 9; }
    r.push(change());
    print(a, " ", r.len);
    print(r[r.len - 1]); // bce:keep
}
