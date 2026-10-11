// error: copying a value of type Node, which contains self-relative references, is not supported; construct it in place
// A pushed element is copied into the new slot, its offsets still measured
// from where the reference points (§3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
fn main() {
    var ns: Node[>..] = [];
    var ms: Node[>..] = [];
    ms.push(pair(ns));
    print(ms[0].v);
}
