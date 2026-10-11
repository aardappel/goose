// error: .pop_n needs fixed-size elements: u8[][>..] is sequential
// pop_n needs fixed-size elements, as pop does: a sequential array cannot
// find where its last elements start (§3.3).
fn main() {
    var w: u8[][>..] = [];
    w.push("ab");
    w.pop_n(1);
}
