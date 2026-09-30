// error: storing a reference rooted at y, which does not outlive the destination
// A holder assigned to a var of a const struct type is stored as into any
// other holder (§9.2): y's block ends before h does.
struct H { r: i64& }
fn main() {
    var x = 1;
    var h: const H = H { r: x };
    {
        var y = 2;
        h = H { r: y };
    }
    print(h.r);
}
