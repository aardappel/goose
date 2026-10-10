// Binarytrees::Obj and Binarytrees::Arena: build a complete binary tree of
// the configured depth, sum its items, discard it, once per run.

namespace binarytrees;

import std;
import bench;

// --- Binarytrees::Obj -----------------------------------------------------------

// The tree's nodes live in one global pool, so a link can be a 4-byte offset
// from the pool's base, null for none. Allocating a node is a push, and
// discarding the whole tree is a clear.
struct TreeNode { left: TreeNode&<u32 in nodes>?, right: TreeNode&<u32 in nodes>?, item: i32 }
var nodes: TreeNode[>..];

recursive fn new_tree_node(item: i32, depth: i32) -> TreeNode& {
    let node .= nodes.push(TreeNode { item: item });
    if depth > 0 {
        node.left .= new_tree_node(item - (1 << (depth - 1)), depth - 1);
        node.right .= new_tree_node(item + (1 << (depth - 1)), depth - 1);
    }
    return node;
}

recursive fn sum(node: TreeNode&) -> u32 {
    var total = (node.item as! u32) + 1;
    let left = node.left;
    if left { total += left.sum(); }
    let right = node.right;
    if right { total += right.sum(); }
    return total;
}

fn run_obj(b: Bench&) -> u32 {
    let n = config_i64(b.name, "depth");
    var result: u32 = 0;
    measure(b) {
        let root = new_tree_node(0, n as i32);
        result += root.sum();
        nodes.clear();
    };
    return result;
}

// --- Binarytrees::Arena ---------------------------------------------------------

// The same tree in a growable array, linked by indices, -1 for none.
struct TreeNodeArena { item: i32, left: i32 = -1, right: i32 = -1 }

recursive fn build_tree(arena: TreeNodeArena[>..]&, item: i32, depth: i32) -> i32 {
    let idx = arena.len as i32;
    arena.push(TreeNodeArena { item: item });
    if depth > 0 {
        let left_idx = arena.build_tree(item - (1 << (depth - 1)), depth - 1);
        let right_idx = arena.build_tree(item + (1 << (depth - 1)), depth - 1);
        let node .= arena[idx];
        node.left = left_idx;
        node.right = right_idx;
    }
    return idx;
}

recursive fn sum(arena: TreeNodeArena[:], idx: i32) -> u32 {
    let node = arena[idx];
    var total = (node.item as! u32) + 1;
    if node.left >= 0 { total += arena.sum(node.left); }
    if node.right >= 0 { total += arena.sum(node.right); }
    return total;
}

fn run_arena(b: Bench&) -> u32 {
    let n = config_i64(b.name, "depth");
    var arena: TreeNodeArena[>..] = [];
    var result: u32 = 0;
    measure(b) {
        arena.clear();
        arena.build_tree(0, n as i32);
        result += arena.sum(0);
    };
    return result;
}
