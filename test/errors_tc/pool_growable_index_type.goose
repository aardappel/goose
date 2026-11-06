// error: cannot pass between threads: it holds an index type
// What a task grows comes from its own thread program, whose copy of the
// array an index type names grows on its own (docs/design/index_types.md).
var nodes: i32[>..] = [];
type NodeId = u32 in nodes;

fn keep(out: NodeId[>..]&, idx: i64) {
    out.push(0 as NodeId);
}

fn main() {
    nodes.push(1);
    var pool = thread_pool(2);
    defer pool.close();
    var out: NodeId[>..] = [];
    pool.apply(chunks(out, 2), keep);
}
