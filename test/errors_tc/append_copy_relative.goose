// error: .append copies the elements of a[1..]: copying a value of type Node, which contains self-relative references, is not supported
// A copied self-relative offset still measures from the source (§3.9):
// b[3].next would land on b[2], not on a copy of a[0].
struct Node { v: i64, next: Node&<u16>? }
fn main() {
    var a: Node[>..] = [];
    a.push(Node { v: 1, next: null });
    a.push(Node { v: 2, next: a[0] });
    var b: Node[>..] = [];
    b.push(Node { v: 7, next: null });
    b.push(Node { v: 8, next: null });
    b.push(Node { v: 9, next: null });
    b.append(a[1..]);
    let nx = b[3].next;
    guard nx else { print("null"); return; }
    print(nx.v);
}
