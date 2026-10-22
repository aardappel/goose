// error: S cannot pass between threads: it holds an index type
// A variant type met before its enum leaves the enum's other variants to
// look at: E.W holds the index type here, after S's E.V field.
var nodes: i64[>..] = [];
enum E { V { x: i64 }, W { i: u32 in nodes } }
struct S { a: E.V, b: E }
fn main() {
    nodes.push(1);
    let s = S { a: E.V { x: 1 }, b: E.W { i: 0 as (u32 in nodes) } };
    qput(s);
}
