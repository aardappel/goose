// A variant with no reference fields, passed or stored as an enum that has
// some, holds nothing (§9.2): it stores anywhere, as a constructor function
// for the nodes of a table does.
enum Kind { Int { v: i64 }, Call { args: const i64[:] } }
struct Node { token: i64, kind: Kind }
var nodes: Node[>..] = [];
var data: i64[>..] = [];

fn mk(k: Kind) -> Node& { return nodes.push(Node { token: 0, kind: k }); }
fn keep(k: Kind) { nodes.push(Node { token: 1, kind: k }); }

fn main() {
    data.push(5);
    let a .= mk(Kind.Int { v: 2 });
    let c .= mk(Kind.Call { args: data[..] });
    keep(Kind.Int { v: 3 });
    print(nodes.len, " ", a.token);
    match c.kind { Int i => {}, Call x => print(x.args.len) }
}
