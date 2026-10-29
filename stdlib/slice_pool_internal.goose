namespace slice_pool_detail;

// A 512-byte B+ node: leaves use 31 keys and 31 counts; inner nodes use
// 20 keys, 20 subtree maxima and 20 child indices in the same typed storage.
// Node 0 stays zero and terminates the chain of emptied nodes.
struct node { n: i64, link: i64, data: i64[62] }
struct tree { root: i64, height: i64, chain: i64 }

// The runtime's span tree, reusable[]'s, in a copy (gs_spc_*) with tree as its
// header. It checks every node number and count it reads from nodes and tree,
// which the program can write, and aborts on one that is wrong.
extern fn place(nodes: node[>..]&, tree: tree&, length: i64, cnt: i64) -> i64 = "gs_slice_pool_place";
extern fn release_run(nodes: node[>..]&, tree: tree&, idx: i64, cnt: i64) = "gs_slice_pool_free";
extern fn regrow(nodes: node[>..]&, tree: tree&, length: i64, idx: i64, old: i64,
                 cnt: i64) -> i64 = "gs_slice_pool_regrow";
