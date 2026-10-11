// Binarytrees::Obj and Binarytrees::Arena: build a complete binary tree of
// the configured depth, sum its items, discard it, once per run.

namespace binarytrees;

import std;
import bench;

// --- Binarytrees::Obj -----------------------------------------------------------

// The tree's nodes live in a pool the run owns, so a link can be a 4-byte
// offset from the link itself, null for none. Allocating a node is a push,
// and the whole tree is freed when the run returns.
struct TreeNode { left: TreeNode&<u32>?, right: TreeNode&<u32>?, item: i32 }

recursive fn new_tree_node(nodes: TreeNode[>..]&, item: i32, depth: i32) -> TreeNode& {
    let node .= nodes.push(TreeNode { item: item });
    if depth > 0 {
        node.left .= new_tree_node(nodes, item - (1 << (depth - 1)), depth - 1);
        node.right .= new_tree_node(nodes, item + (1 << (depth - 1)), depth - 1);
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

struct BinarytreesObj { n: i64, result_val: u32 }

fn new_binarytrees_obj() -> BinarytreesObj {
    BinarytreesObj { n: config_i64("Binarytrees::Obj", "depth"), result_val: 0 }
}

fn prepare(s: BinarytreesObj&) {}

fn run(s: BinarytreesObj&, iteration_id: i64) {
    var nodes: TreeNode[>..] = [];
    let root .= new_tree_node(nodes, 0, s.n as i32);
    s.result_val += root.sum();
}

fn checksum(s: BinarytreesObj&) -> u32 { s.result_val }

// --- Binarytrees::Arena ---------------------------------------------------------

// The same tree in a growable array, linked by indices, -1 for none.
struct TreeNodeArena { item: i32, left: i32 = -1, right: i32 = -1 }

// The arena is growable, so it is the last field.
struct BinarytreesArena { n: i64, result_val: u32, arena: TreeNodeArena[>..] }

fn new_binarytrees_arena() -> BinarytreesArena {
    BinarytreesArena { n: config_i64("Binarytrees::Arena", "depth"), result_val: 0, arena: [] }
}

recursive fn build_tree(s: BinarytreesArena&, item: i32, depth: i32) -> i32 {
    let idx = s.arena.len as i32;
    s.arena.push(TreeNodeArena { item: item });
    if depth > 0 {
        let left_idx = s.build_tree(item - (1 << (depth - 1)), depth - 1);
        let right_idx = s.build_tree(item + (1 << (depth - 1)), depth - 1);
        let node .= s.arena[idx];
        node.left = left_idx;
        node.right = right_idx;
    }
    return idx;
}

recursive fn sum(s: BinarytreesArena&, idx: i32) -> u32 {
    let node = s.arena[idx];
    var total = (node.item as! u32) + 1;
    if node.left >= 0 { total += s.sum(node.left); }
    if node.right >= 0 { total += s.sum(node.right); }
    return total;
}

fn prepare(s: BinarytreesArena&) {}

fn run(s: BinarytreesArena&, iteration_id: i64) {
    s.arena = [];
    s.build_tree(0, s.n as i32);
    s.result_val += s.sum(0);
}

fn checksum(s: BinarytreesArena&) -> u32 { s.result_val }
