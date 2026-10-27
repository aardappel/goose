// error: cannot call zap, which shrinks data while r
// A field default runs at the construction that takes it, so its shrink is
// judged against main's live r (§3.2, §5.2).
var data: i64[>..<] = [42];
fn zap() -> i64 { data.clear(); 0 }
struct S<T> { x: i64 = zap() }
fn main() {
    let r .= data[0];
    let s = S<u8> {};
    print(r);
}
