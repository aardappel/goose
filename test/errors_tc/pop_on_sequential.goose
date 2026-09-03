// pop needs fixed-size elements: a sequential array cannot find its last
// element (§3.3).
fn main() {
    var w: u8[][>..] = [];
    w.push("ab");
    w.pop();
}
