// error: cannot infer the element type of []: a block's value is checked without its call's destination
// A block's value is checked where the block is written, not at the
// destination its call's value meets (§7.6), so the u8[] the call initializes
// gives the [] no element type.
fn count<F>() -> i64 {
    let x: u8[] = F();
    return x.len;
}
fn main() {
    print(count() { [] });
}
