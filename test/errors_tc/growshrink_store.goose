// A slice of a grow-shrink array may not be stored (§5.2).
struct H { s: i64[:] }
fn main() {
    var gs: i64[>..<] = [1, 2, 3];
    let h = H { s: gs[0..2] };
    print(h.s.len);
}
