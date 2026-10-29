// runtime-debug: a child number edited past the nodes aborts in the runtime's span tree.
import slice_pool;
fn main() {
    var p = slice_pool<i64> {};
    var runs: i64[:][>..] = [];
    for i in 80 { runs.push(p.alloc_slice(1)); }
    for i in 40 { p.free_slice(runs[2 * i]); }
    assert(p.tree.height == 1);
    p.nodes[p.tree.root].data[41] = 1000000;
    p.alloc_slice(2);
}
