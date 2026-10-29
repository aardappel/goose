// Index identities share the array-field places used by bounds analysis.
import std;
root struct Graph { nodes: i32[>..] = [], edges: (u32 in graph.nodes)[>..] = [], scratch: i32[>..] = [] }
var graph = Graph {};
type NodeId = u32 in graph.nodes;
type EdgeEnd = u16 upto graph.edges;

fn get(i: NodeId) -> i32 { graph.nodes[i] }   // bce:elide
fn reset(a: i32[>..]&) { a.clear(); }
fn main() {
    graph.nodes.push(10);
    graph.nodes.push(20);
    graph.edges.push(1 as NodeId);            // bce:elide
    graph.edges.push(0 as NodeId);            // bce:elide
    let lo = 0 as EdgeEnd;
    let hi = graph.edges.len as EdgeEnd;      // bce:elide
    var total = 0;
    for k in lo..hi { total += get(graph.edges[k]); } // bce:elide
    print(total);
    graph.scratch.push(7);
    reset(graph.scratch);
    let back, ok = from_bytes<NodeId[>..]>(to_bytes(graph.edges));
    print(ok, " ", get(back[0]));
    var bad: u32[>..] = [2];
    let decoded, valid = from_bytes<NodeId[>..]>(to_bytes(bad));
    print(valid, " ", decoded.len);
}
