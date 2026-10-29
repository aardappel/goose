// runtime-debug: a chain of emptied nodes edited to lead past the nodes aborts when a node is taken from it.
import slice_pool;
fn main() {
    var p = slice_pool<i64> {};
    var runs: i64[:][>..] = [];
    for i in 80 { runs.push(p.alloc_slice(1)); }
    p.free_slice(runs[0]);
    p.tree.chain = 77;
    for i in 1..40 { p.free_slice(runs[2 * i]); }
}
