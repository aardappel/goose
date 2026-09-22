var data: i64[>..<] = [42];
fn zap() -> i64 { data.clear(); 0 }
struct S { x: i64 = zap() }
fn main() {
    if data.len > 0 {
        let s = S {};
        print(data[0]);
    }
}
