// error: cannot call relabeled in the value assigned to b: relabeled uses b
// Assigning a value that holds a resizable array rebuilds that array in
// place too (§4.4).
struct Box { tag: i64, items: u8[>..] }
fn main() {
    var b = Box { tag: 1, items: [] };
    b.items.append("abcd");
    fn relabeled() -> Box {
        var out = Box { tag: 2, items: [] };
        for x in b.items { out.items.push(x + 1); }
        for x in b.items { out.items.push(x + 2); }
        return out;
    }
    b = relabeled();
    print(b.tag, " ", b.items);
}
