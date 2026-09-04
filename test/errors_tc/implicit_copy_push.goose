// A non-fixed lvalue is not copied implicitly into an element (§4.1).
fn main() {
    var words: u8[][>..] = ["ab"];
    var out: u8[][>..] = [];
    for w in words { out.push(w); }
}
