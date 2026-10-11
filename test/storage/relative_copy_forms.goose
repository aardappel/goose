// What a value holding self-relative references may still do without being
// copied out of the storage its links point within (§3.9): a reference
// result bound with `.=`, passed to a reference or untyped parameter or read
// through; a by-value result, built where it is received, and a default;
// a construct whose branches a reference parameter binds by reference; a
// popped element that is discarded or whose scalar field is read; a named
// result whose type is inferred (§7.3); and a value linked only `in pool`,
// which copies like any other.
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

fn left_item(t) -> i32 {
    let l = t.left;
    guard l else { return -1; }
    return l.item;
}

fn ends(nodes: TreeNode[>..]&) -> TreeNode&, TreeNode& {
    return nodes[0], nodes[nodes.len - 1];
}

struct Ring { next: Ring&<u32>, v: i64 }

fn lone(v: i64) -> Ring { return Ring { next: self, v: v }; }

fn follow(r: Ring&) -> i64 { return r.next.v; }

fn chain(n: i64) {
    var rs: Ring[>..] = [];
    rs.push(Ring { next: self, v: 0 });
    for i in n - 1 { rs.push(Ring { next: rs[i], v: i + 1 }); }
    return rs;
}

struct Pooled { next: Pooled&<u32 in pool>?, v: i64 }
var pool: Pooled[>..] = [];

fn main() {
    var nodes: TreeNode[>..] = [];
    let root .= build(nodes, 5, 3);
    print(root.sum(), " ", nodes.len, " ", left_item(root));
    var more: TreeNode[>..] = [];
    print(sum(build(more, 1, 2)), " ", left_item(build(more, 9, 1)));
    let r = build(more, 2, 1).right;
    if r { print(r.item); }
    let first, last .= ends(nodes);
    print(first.item, " ", last.item);

    let one = lone(7);
    print(one.next.v, " ", lone(8).next.v);
    let d = default<TreeNode>();
    print(d.item, " ", d.left == null);

    var rs: Ring[>..] = [];
    let a .= rs.push(Ring { next: self, v: 1 });
    rs.push(Ring { next: a, v: 2 });
    let c = rs.len > 1;
    print(follow(if c { rs[1] } else { rs[0] }));
    print(rs.pop().v, " ", rs.len);
    rs.pop();
    print(rs.len);

    let ch = chain(4);
    print(ch[3].next.next.v, " ", ch[0].next.v);

    pool.push(Pooled { v: 1 });
    pool.push(Pooled { next: pool[0], v: 2 });
    let x = pool[1];
    let y = copy(pool[1]);
    let xn = x.next;
    let yn = y.next;
    if xn { if yn { print(xn.v, " ", yn.v, " ", x.v + y.v); } }
    let pn = pool.pop().next;
    if pn { print(pn.v); }
}
