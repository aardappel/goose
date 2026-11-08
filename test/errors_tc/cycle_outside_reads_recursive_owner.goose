// error: references may only be passed down, not stored, inside a recursive cycle
// The stack eval reads its nodes back from is a local of outer, which is
// recursive itself: each of its activations has one, so neither the entry
// call's node nor what is read back off the stack points outside every
// cycle (§7.8).
struct Node { first: Node&?, next: Node&?, n: i64 }
recursive fn outer(d: i64) -> i64 {
    var tree: Node[>..] = [];
    var out: Node&[>..<] = [];
    recursive fn eval(node: Node&, n: i64) {
        out.push(node);
        if n > 0 { for i in 0..out.len { eval(out[i], n - 1); } }
    }
    let root = tree.push(Node { first: null, next: null, n: d });
    eval(root, 1);
    if d > 0 { return outer(d - 1) + out.len; }
    return out.len;
}
fn main() { print(outer(2)); }
