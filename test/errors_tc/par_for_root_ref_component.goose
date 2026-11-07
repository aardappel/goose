// error: v may point at total, a reduction of this par for
// A root struct's component holding a reference leads where it points: a
// nested function reading v.r reads the reduction (docs/design/child_threads.md
// §4.6).
root struct V { r: i64& }
fn main() {
    var total = 1;
    let v = V { r: total };
    fn read() -> i64 { v.r }
    par for k in 4 { total += read(); }
    print(total);
}
