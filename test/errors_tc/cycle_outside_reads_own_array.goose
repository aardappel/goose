// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes node rooted differently from the cycle's entry call
// A back edge passes node an element of the calling activation's own
// array: storage that dies with the activation, not storage the cycle may
// store into, so node is pass-down only (§7.8).
struct Node { first: Node&?, next: Node&?, n: i64 }
fn main() {
    var tree: Node[>..] = [];
    var out: Node&[>..<] = [];
    recursive fn eval(node: Node&, n: i64) {
        out.push(node);
        if n > 0 {
            var mine: Node[2] = [Node { first: null, next: null, n: 7 },
                                 Node { first: null, next: null, n: 8 }];
            for i in 0..out.len { eval(out[i], n - 1); }
            eval(mine[1], n - 1);
        }
    }
    let root = tree.push(Node { first: null, next: null, n: 0 });
    eval(root, 2);
    for o in out { print(o.n); }
}
