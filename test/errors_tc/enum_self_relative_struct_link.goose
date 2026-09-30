// error: enum L has payloads holding self-relative references
// error: use it in variable mode (L..)
// A struct linked to others of its kind through a fixed-mode ADT field.
struct B { w: i64, l: L }
enum L { End, Next { b: B&<u32> } }
fn main() {
    var bs: B[>..] = [];
    bs.push(B { w: 1, l: L.End });
    bs.push(B { w: 2, l: L.Next { b: bs[0] } });
    match bs[1].l { Next n => print(n.b.w), End => {} }
}
