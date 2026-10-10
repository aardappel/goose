// A copy of a field or an element of a local holder -- a `let`, a `for`
// binder, a popped element, a value loaded through a reference -- holds no
// more than was stored into the local, which is on record (§9.5): views of
// a pool stored into a local stack go on into the pool, and an element
// copied out of an array of holders with null links holds nothing.
import std;

enum Op { Leaf, Loop { body: Op[:] } }

// The program's loop bodies as runs of `pool`, inner ones first: the nested
// recursive parser collects the ops of the bodies still open on the
// enclosing function's `pending` stack, and appends each finished body to
// the pool. A body's loops hold views of the pool, which pending holds on
// their way into it.
fn build(code: const u8[:], pool: Op[>..]&) -> Op[:] {
    var pending: Op[>..<] = [];
    var pos = 0;
    recursive fn parse() -> Op[:] {
        let mark = pending.len;
        while pos < code.len {
            let c = code[pos];
            pos++;
            if c == '[' { pending.push(Op.Loop { body: parse() }); }
            else if c == ']' { break; }
            else { pending.push(Op.Leaf); }
        }
        let start = pool.len;
        pool.append(pending[mark..]);
        pending.resize(mark);
        return pool[start..];
    }
    return parse();
}

recursive fn count(ops: Op[:]) -> i64 {
    var n = 0;
    for op in ops {
        match op {
            Leaf => { n++; },
            Loop l => { n += 1 + count(l.body); },
        }
    }
    return n;
}

// The same through a pop, a `for` binder and a `let` copy.
fn moved(pool: Op[>..]&) -> i64 {
    var stack: Op[>..<] = [];
    stack.push(Op.Loop { body: pool[0..1] });
    stack.push(Op.Loop { body: pool[1..2] });
    let start = pool.len;
    pool.push(stack.pop());
    for op in stack { pool.push(op); }
    let first = stack[0];
    pool.push(first);
    return pool.len - start;
}

struct B { v: f64 }
struct N { v: f64, link: B? }

fn third() -> N {
    var xs: N[3] = [N { v: 1.0, link: null }, N { v: 2.0, link: null }, N { v: 3.0, link: null }];
    let x = xs[2];
    return x;
}

fn main() {
    var pool: Op[>..] = [];
    let prog = build("a[b[c]d]e", pool);
    print(count(prog), " ", pool.len);
    print(moved(pool));
    print(third().v);
}
