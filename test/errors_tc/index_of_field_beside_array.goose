// error: .index_of needs a reference rooted at the array itself
// error: this one is rooted at b, which may hold i64 values outside that array
// A field of the element type beside the array is no element of it: its
// distance from the array need not even be a whole number of elements (§3.3).
struct Bag { tag: i64, items: i64[>..] }
fn main() {
    var b = Bag { tag: 1, items: [10, 20] };
    let r: i64& = b.tag;
    print(b.items.index_of(r));
}
