// error: cannot use back in the value assigned to s.tail: back may refer to s.tail
// A reference to storage that can hold the struct by value may refer to the
// storage the tail lies in (§4.4).
struct D { n: i64, tail: u8[>..] }
struct W { d: D }
fn render(w: W&) -> u8[>..] {
    var o: u8[>..] = [];
    o.append(w.d.tail);
    return o;
}
fn run(s: D&, back: W&) { s.tail = render(back); }
fn main() {
    var w = W { d: D { n: 0, tail: [1] } };
    run(w.d, w);
    print(w.d.tail);
}
