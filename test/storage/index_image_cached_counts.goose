// Image verification observes growth even when the caller caches the count.
import std;
var nodes: i32[>..] = [];
type Id = u32 in nodes;
struct Edge { target: Id }
fn main() {
    nodes.push(10);
    nodes.push(20);
    var edges: Edge[>..] = [Edge { 1 as Id }];
    let back, ok = from_bytes<Edge[>..]>(to_bytes(edges));
    assert(ok);
    print(nodes[back[0].target]);
}
