// error: an array literal of variable-size elements is a Txt[]
// bytes_of is a view of its argument's elements, which a literal of
// variable-size elements has no temporary to give it (§4.2).
struct Txt { body: u8[] }
fn main() {
    let b = bytes_of([Txt { body: "ab" }]);
    print(b.len);
}
