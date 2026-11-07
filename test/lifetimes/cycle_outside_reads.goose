// A parameter that every call into the cycle gives the entry call's
// reference, the parameter itself, or a reference read out of storage the
// cycle may store into points outside the cycle in every activation, so
// what it points at may be stored there (§7.8). Here a jq-like evaluator
// keeps its values on one grow-shrink stack and feeds each value it pushed
// back to the next filter: with the stack a free variable of main, a pool
// parameter, or a field of a context passed around a cycle of two
// functions, and with a recursive call's result stored too.
struct Node { first: Node&?, next: Node&?, n: i64 }
enum F { Identity, Children, Pipe { a: i64, b: i64 } }

recursive fn eval_pool(fs: F[3]&, out: Node&[>..<]&, f: i64, node: Node&) {
    match fs[f] {
        Identity => { out.push(node); },
        Children => { var k = node.first; while k != null { out.push(k); k .= k.next; } },
        Pipe p => {
            let mark = out.len;
            eval_pool(fs, out, p.a, node);
            let end = out.len;
            for i in mark..end { eval_pool(fs, out, p.b, out[i]); }
        },
    }
}

struct Ctx { out: Node&[>..<]&, fs: F[3] }
var seen: i64[>..] = [];

recursive fn eval_ctx(c: Ctx&, f: i64, node: Node&) {
    seen.push(node.n);
    match c.fs[f] {
        Identity => { c.out.push(node); },
        Children => { var k = node.first; while k != null { c.out.push(k); k .= k.next; } },
        Pipe p => {
            let mark = c.out.len;
            eval_ctx(c, p.a, node);
            let end = c.out.len;
            for i in mark..end { each(c, p.b, c.out[i]); }
        },
    }
}

fn each(c: Ctx&, f: i64, node: Node&) { eval_ctx(c, f, node); }

fn main() {
    var tree: Node[>..] = [];
    var out: Node&[>..<] = [];
    let fs: F[3] = [F.Children, F.Children, F.Pipe { a: 0, b: 1 }];
    recursive fn eval(f: i64, node: Node&) {
        match fs[f] {
            Identity => { out.push(node); },
            Children => { var k = node.first; while k != null { out.push(k); k .= k.next; } },
            Pipe p => {
                let mark = out.len;
                eval(p.a, node);
                let end = out.len;
                for i in mark..end { eval(p.b, out[i]); }
            },
        }
    }
    let leaf = tree.push(Node { first: null, next: null, n: 2 });
    let leaf2 = tree.push(Node { first: null, next: null, n: 3 });
    leaf.next .= leaf2;
    let mid = tree.push(Node { first: leaf, next: null, n: 1 });
    let root = tree.push(Node { first: mid, next: null, n: 0 });
    eval(2, root);
    for o in out { print(o.n); }

    var pool: Node&[>..<] = [];
    eval_pool(fs, pool, 2, root);
    print(pool.len, " ", pool[pool.len - 1].n);

    var cout: Node&[>..<] = [];
    var c = Ctx { out: cout, fs: fs };
    eval_ctx(c, 2, root);
    print(cout.len, " ", seen.len);

    var picked: Node&[>..<] = [];
    recursive fn pick(node: Node&, n: i64) -> Node& {
        if n == 0 { return node; }
        let end = picked.len;
        for i in 0..end {
            let r: Node& = pick(picked[i], n - 1);
            picked.push(r);
        }
        return node;
    }
    picked.push(mid);
    let r: Node& = pick(root, 2);
    print(picked.len, " ", r.n);
}
