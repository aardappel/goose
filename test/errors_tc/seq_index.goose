// error: arrays of variable-size elements cannot be indexed, only iterated; for random access, keep your own array of references to them (§3.3)
// Arrays of variable-size elements cannot be indexed (§3.3).
fn main() {
    var a: u8[][>..] = [];
    a.push("x");
    print(a[0].len);
}
