// error: .chunk needs fixed-size elements: u8[][] is sequential
// Runs of variable-size elements have no offsets to cut at, as an index
// into them has none (§3.3).
fn main() {
    let names: u8[][] = ["a", "bb", "ccc"];
    print(names.chunk(0, 2));
}
