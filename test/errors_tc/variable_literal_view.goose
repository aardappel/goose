// error: an array literal of variable-size elements is a Txt[]
// Slicing a literal in place views it as a temporary; with variable-size
// elements there is none (§4.2), so the slice would outlive its storage.
struct Txt { body: u8[] }
fn main() {
    let s = [Txt { body: "ab" }, Txt { body: "c" }][..];
    print(s.len);
}
