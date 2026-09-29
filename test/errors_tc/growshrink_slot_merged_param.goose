// error: cannot pop e while v
// error: is still used: it may refer into e
// One branch of v is read out of a field of e's element, the other views
// that element's buf, which lies in the grow-shrink array e names (§5.2):
// the merged value may be either, a slot read or not.
struct Entry { text: u8[:], buf: u8[4] }
fn f(e: Entry[>..<]&, c: bool) {
    let v = if c { e[0].text } else { e[0].buf[..] };
    e.pop();
    print(v);
}
fn main() {
    var chars: u8[>..] = [];
    chars.append("ab");
    var es: Entry[>..<] = [Entry { text: chars[..], buf: [1, 2, 3, 4] }];
    f(es, false);
}
