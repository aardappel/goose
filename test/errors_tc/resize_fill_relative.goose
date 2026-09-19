// error: .resize copies its fill value into every slot it adds: copying a value of type Node, which contains self-relative references, is not supported
// The fill value is built once and copied, so its offset to a[0] would
// measure from where it was built, not from each slot (§3.9).
struct Node { v: i64, next: Node&<u16>? }
fn main() {
    var a: Node[..8] = [];
    a.push(Node { v: 1, next: null });
    a.resize(4, Node { v: 2, next: a[0] });
    for i in a.len {
        let nx = a[i].next;
        guard nx else { print("null"); continue; }
        print(nx.v);
    }
}
