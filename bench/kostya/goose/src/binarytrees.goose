// Binarytrees::Obj and Binarytrees::Arena: build a complete binary tree of
// the configured depth, sum its items, discard it, once per run.
//
// Both builders handle a leaf first, with an early return: a recursive
// function that starts that way has the compiler build the leaves at their
// parent's call site, which saves the call for half of the nodes.

namespace binarytrees;

import std;
import bench;

// --- Binarytrees::Obj -----------------------------------------------------------

// The tree's nodes live in one global pool, so a link can be a 4-byte offset
// from the pool's base, null for none. Allocating a node is a push, and
// discarding the whole tree is a clear.
struct TreeNode { item: i32, left: TreeNode&<u32 in nodes>?, right: TreeNode&<u32 in nodes>? }
var nodes: TreeNode[>..];

recursive fn make_tree(item: i32, depth: i32) -> TreeNode& {
    if depth == 0 { return nodes.push(TreeNode { item: item }); }
    let node .= nodes.push(TreeNode { item: item });
    node.left .= make_tree(item - (1 << (depth - 1)), depth - 1);
    node.right .= make_tree(item + (1 << (depth - 1)), depth - 1);
    return node;
}

recursive fn sum_tree(node: TreeNode&) -> u32 {
    var total = (node.item as! u32) + 1;
    let left = node.left;
    if left { total += sum_tree(left); }
    let right = node.right;
    if right { total += sum_tree(right); }
    return total;
}

fn run_obj(b: Bench&) -> u32 {
    let n = config_i64(b.name, "depth") as i32;
    var result: u32 = 0;
    measure(b) {
        result += sum_tree(make_tree(0, n));
        nodes.clear();
    };
    return result;
}

// --- Binarytrees::Arena ---------------------------------------------------------

// The same tree in a growable array, linked by indices, -1 for none.
struct ArenaNode { item: i32, left: i32 = -1, right: i32 = -1 }
var arena: ArenaNode[>..];

fn new_node(item: i32) -> i32 {
    arena.push(ArenaNode { item: item });
    return (arena.len - 1) as i32;
}

recursive fn build_tree(item: i32, depth: i32) -> i32 {
    if depth == 0 { return new_node(item); }
    let idx = new_node(item);
    let left_idx = build_tree(item - (1 << (depth - 1)), depth - 1);
    let right_idx = build_tree(item + (1 << (depth - 1)), depth - 1);
    let node .= arena[idx];
    node.left = left_idx;
    node.right = right_idx;
    return idx;
}

recursive fn sum_nodes(idx: i32) -> u32 {
    let node = arena[idx];
    var total = (node.item as! u32) + 1;
    if node.left >= 0 { total += sum_nodes(node.left); }
    if node.right >= 0 { total += sum_nodes(node.right); }
    return total;
}

fn run_arena(b: Bench&) -> u32 {
    let n = config_i64(b.name, "depth") as i32;
    var result: u32 = 0;
    measure(b) {
        arena.clear();
        build_tree(0, n);
        result += sum_nodes(0);
    };
    return result;
}
