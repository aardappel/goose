// No slices of grow-shrink arrays (§3.10).
fn main() {
    var gs: int[>..<] = [1, 2, 3];
    let s = gs[0..2];
    print(s.len);
}
