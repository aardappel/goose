// error: cannot use s.other in the value assigned to s.tail
// A field referring to an array of the tail's type may refer to the tail
// itself, which the right-hand side would read while it is rebuilt (§4.4).
struct D { n: i64, other: u8[>..]&, tail: u8[>..] }
fn render(r: u8[>..]&) -> u8[>..] {
    var o: u8[>..] = [];
    o.append(r);
    return o;
}
fn run(s: D&) { s.tail = render(s.other); }
fn main() {
    var spare: u8[>..] = [];
    var d = D { n: 0, other: spare, tail: [1] };
    d.other .= d.tail;
    run(d);
    print(d.tail);
}
