// error: varint is a storage type: only fields and array elements
// varint is a storage type for fields and elements, not locals (§3.6).
fn main() {
    var v: varint = 1;
    print(v);
}
