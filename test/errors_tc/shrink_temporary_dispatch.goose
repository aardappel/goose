enum E { A { r: i64& }, B { r: i64& } }
fn read(v: E.A, n: i64) { print(v.r); }
fn read(v: E.B, n: i64) { print(v.r); }
fn wrap(r: i64&) -> E { E.A { r } }
fn main() {
    var a: i64[>..<] = [42];
    read(wrap(a[0]), a.pop());
}
