// error: .range_index needs fixed-size elements
fn main() {
    var a: u8[][>..] = ["a"];
    print(a.range_index(a[..]));
}
