// A [] breaking out of a block takes no element type from an un-annotated let.
fn main() {
    let a = block { break []; };
    print(a.len);
}
