// error: h may point at total, a reduction of this par for
// A holder of a reference to a reduction, handed to a callee that reads
// through it (docs/design/child_threads.md §4.6).
struct H { r: i64& }
fn get(h: H) -> i64 { h.r }
fn main() {
    var total = 1;
    let h = H { r: total };
    par for k in 8 { total += get(h); }
    print(total);
}
