// No references into the interior of a grow-shrink array (§5.2).
fn main() {
    var gs: int[>..<] = [1, 2, 3];
    let r = &gs[0];
    print(r);
}
