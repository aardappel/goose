// runtime-debug: a pop_n shrinks the array its receiver named before the count rebound it.
fn main() {
    var a: i64[>..<] = [1, 2, 3, 4];
    var b: i64[>..<] = [5];
    var r .= a;
    fn change() -> i64 { r .= b; return 2; }
    r.pop_n(change());
    print(a, " ", r.len);
    print(r[1]); // bce:keep
}
