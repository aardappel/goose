// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes node rooted differently from the cycle's entry call
// One back edge passes node a reference read out of main's stack, but the
// other passes a local of the calling activation, so node does not point
// outside the cycle in every activation, and what it points at is not
// stored there (§7.8).
struct Node { first: Node&?, next: Node&?, n: i64 }
enum F { Identity, Children, Pipe { a: i64, b: i64 } }
fn main() {
    var tree: Node[>..] = [];
    var out: Node&[>..<] = [];
    let fs: F[3] = [F.Identity, F.Children, F.Pipe { a: 0, b: 1 }];
    recursive fn eval(f: i64, node: Node&) {
        match fs[f] {
            Identity => { out.push(node); },
            Children => { var k = node.first; while k != null { out.push(k); k .= k.next; } },
            Pipe p => {
                var mine = Node { first: null, next: null, n: 9 };
                let end = out.len;
                for i in 0..end { eval(p.b, out[i]); }
                eval(p.a, mine);
            },
        }
    }
    let root = tree.push(Node { first: null, next: null, n: 0 });
    eval(2, root);
    for o in out { print(o.n); }
}
