// error: copying a value of type TreeNode, which contains self-relative references, is not supported; construct it in place, or bind a reference to it with .=: let root .= build(nodes, 0, 10)
// An un-annotated `let` of a reference-returning call copies the pointee
// (§3.8), whose offsets would then be measured from the copy: sum would
// follow the root's links from the stack (§3.9).
struct TreeNode { left: TreeNode&<u32>?, right: TreeNode&<u32>?, item: i32 }
recursive fn build(nodes: TreeNode[>..]&, item: i32, depth: i32) -> TreeNode& {
    let node .= nodes.push(TreeNode { item: item });
    if depth > 0 {
        node.left .= build(nodes, item - 1, depth - 1);
        node.right .= build(nodes, item + 1, depth - 1);
    }
    return node;
}
recursive fn sum(node: TreeNode&) -> i64 {
    var total = node.item as i64;
    let l = node.left;
    if l { total += l.sum(); }
    let r = node.right;
    if r { total += r.sum(); }
    return total;
}
fn main() {
    var nodes: TreeNode[>..] = [];
    let root = build(nodes, 0, 10);
    print(root.sum());
}
