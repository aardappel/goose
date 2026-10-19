// Printing an array of variable-size records takes their fields' pointers
// from the element cursor, which must not be const (the runner compiles
// generated C with discarded qualifiers as an error).
struct Item { sku: u8[varint], qty: varint }
struct Order { id: varint, items: Item[varint] }
fn main() {
    var book: Order[>..] = [];
    book.push(Order { id: 1, items: [Item { sku: "A-1", qty: 2 }, Item { sku: "B", qty: 1 }] });
    for o in book { print(o); }
}
