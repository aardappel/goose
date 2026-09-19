// error: cannot assign b while s
// error: is still used: it may refer into b
// Assigning a value that holds a resizable array whole is a shrink of that
// array (§5.2).
struct Bag { tag: i64, items: i64[>..<] }
fn fresh() -> Bag { return Bag { tag: 2, items: [9] }; }
fn main() {
    var b = Bag { tag: 1, items: [1, 2, 3] };
    let s = b.items[..];
    b = fresh();
    print(s);
}
