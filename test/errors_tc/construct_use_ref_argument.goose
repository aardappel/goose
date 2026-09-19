// error: cannot use w in the value assigned to w: that value is built over the old contents of w
// The array handed by reference to the function building its new contents.
fn mk6(v: u8[>..]&) -> u8[>..] {
    var out: u8[>..] = [];
    for x in v { out.push(x + 1); }
    for x in v { out.push(x + 2); }
    return out;
}
fn main() {
    var w: u8[>..] = [];
    w.append("abcdefgh");
    w = mk6(w);
    print(w);
}
