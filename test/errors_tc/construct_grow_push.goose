// error: cannot call mk, which grows a: the element pushed into a is still under construction, and the growth would land inside it (§1.3)
// A variable-size element is built in place at the array's top, and the
// callee producing it pushes into the same array (§1.3(4)).
fn mk(a: u8[][>..]&) -> u8[] { a.push("x"); return "y"; }
fn main() {
    var a: u8[][>..] = [];
    a.push(mk(a));
    print(a.len);
}
