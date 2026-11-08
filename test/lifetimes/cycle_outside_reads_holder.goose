// The same for a parameter of a by-value type holding a reference: what
// its reference points at is judged as a reference parameter's is (§7.8).
// The holder the entry call builds refers into the document, and each back
// edge passes the holder on or one read back off the shared stack, which a
// free variable of main or a context's field holds; a plain reference
// parameter may be given the reference read out of such a holder too.
struct Node { first: Node&?, next: Node&?, n: i64 }
enum Val { Ref { node: Node& }, Num { v: i64 } }
enum F { Identity, Children, Pipe { a: i64, b: i64 } }
struct Ctx { out: Val[>..<]&, fs: F[3] }

recursive fn eval_ctx(c: Ctx&, f: i64, v: Val) {
    match c.fs[f] {
        Identity => { c.out.push(v); },
        Children => {
            match v {
                Ref r => {
                    var k = r.node.first;
                    while k != null { c.out.push(Val.Ref { node: k }); k .= k.next; }
                },
                _ => {},
            }
        },
        Pipe p => {
            let mark = c.out.len;
            eval_ctx(c, p.a, v);
            let end = c.out.len;
            for i in mark..end { eval_ctx(c, p.b, c.out[i]); }
        },
    }
}

recursive fn eval_node(c: Ctx&, f: i64, node: Node&) {
    match c.fs[f] {
        Identity => { c.out.push(Val.Ref { node: node }); },
        Children => { var k = node.first; while k != null { c.out.push(Val.Ref { node: k }); k .= k.next; } },
        Pipe p => {
            let mark = c.out.len;
            eval_node(c, p.a, node);
            let end = c.out.len;
            for i in mark..end { match c.out[i] { Ref r => { eval_node(c, p.b, r.node); }, _ => {} } }
        },
    }
}

fn show(vals: Val[>..<]&) {
    for o in vals { match o { Ref r => { print(r.node.n); }, Num n => { print("#", n.v); } } }
}

fn main() {
    var tree: Node[>..] = [];
    var out: Val[>..<] = [];
    let fs: F[3] = [F.Children, F.Children, F.Pipe { a: 0, b: 1 }];
    recursive fn eval(f: i64, v: Val) {
        match fs[f] {
            Identity => { out.push(v); },
            Children => {
                match v {
                    Ref r => {
                        var k = r.node.first;
                        while k != null { out.push(Val.Ref { node: k }); k .= k.next; }
                    },
                    _ => {},
                }
            },
            Pipe p => {
                let mark = out.len;
                eval(p.a, v);
                let end = out.len;
                for i in mark..end { eval(p.b, out[i]); }
            },
        }
    }
    let leaf = tree.push(Node { first: null, next: null, n: 2 });
    let mid = tree.push(Node { first: leaf, next: null, n: 1 });
    let root = tree.push(Node { first: mid, next: null, n: 0 });
    out.push(Val.Num { v: 9 });
    eval(2, Val.Ref { node: root });
    show(out);

    var cout: Val[>..<] = [];
    var c = Ctx { out: cout, fs: fs };
    eval_ctx(c, 2, Val.Ref { node: root });
    eval_node(c, 2, root);
    show(cout);
}
