// runtime-debug: a push grows the limited array its receiver named before the argument rebound it.
fn main() {
    var a: i64[..4] = [1];
    var b: i64[..4] = [];
    var r .= a;
    fn change() -> i64 { r .= b; return 9; }
    r.push(change());
    print(a, " ", r.len);
    print(r[0]); // bce:keep
}
