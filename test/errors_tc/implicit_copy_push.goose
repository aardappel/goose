// error: w is not fixed-size and is not copied implicitly
// error: pass copy(w) for a copy, or bind it by reference
// A non-fixed lvalue is not copied implicitly into an element (§4.1).
fn main() {
    var words: u8[][>..] = ["ab"];
    var out: u8[][>..] = [];
    for w in words { out.push(w); }
}
