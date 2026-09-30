// error: enum Doc is resizable (its variant Body's payload is)
// error: the payload of its variant Note holds self-relative references
// A self-relative reference anywhere in a payload that is not itself
// resizable counts, here in a struct beside a variable-size field, whatever
// makes the enum resizable: here a struct ending in a grow-only array.
struct Mark { at: i64, prev: Mark&<u8>? }
struct Text { len: i64, chars: u8[>..] }
enum Doc { Empty, Body { t: Text }, Note { tag: u8[], m: Mark } }
fn main() {
    var d: Doc.. = Doc.Empty;
    print(d);
}
