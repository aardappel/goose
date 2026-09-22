// error: cannot call zap
var data: i64[>..<] = [42];
fn zap() -> i64 { data.clear(); 0 }
struct S<T> { x: i64 = zap() }
fn main() {
    let r .= data[0];
    let s = S<u8> {};
    print(r);
}
