// Arrays of variable-size elements cannot be indexed (§3.3).
fn main() {
    var a: u8[][>..] = [];
    a.push("x");
    print(a[0].len);
}
