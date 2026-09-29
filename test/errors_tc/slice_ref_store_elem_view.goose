// error: cannot clear text while qs is still used: it may hold a reference or slice into it
// f stores into qs an element of the slice vs holds, a view of text: not
// the slice vs holds, views, whose elements hold references, but something
// they lead to, which only vs's depth bounds (§9.2), so qs may view text.
fn f(p: (u8[:])[:]&, q: u8[:]&) { q = p[0]; }
fn main() {
    var text: u8[>..] = [];
    text.append("xyz");
    var other: u8[>..] = [];
    other.append("ab");
    var views: (u8[:])[>..] = [];
    views.push(text[..]);
    let vs = views[..];
    let qs = other[..];
    f(vs, qs);
    text.clear();
    text.append("QQQ");
    print(qs);
}
