// A call's runtime-capacity limited result appended to a grow-only and a
// grow-shrink array (§3.3): only its elements join the run, not the
// capacity and length stored ahead of them or the unused slots behind them.
// Calls to functions -O2 keeps (run, wide) and to one it inlines (small), a
// case-function dispatch, and a function value given as a block and as a
// named function; a receiver reached through a reference, i32 elements,
// results with no elements to add, and a limited receiver too. A queue
// read's is in append_limited_queue.goose, which TinyCC does not run.
enum Shape { Circle { r: i64 }, Sq { s: i64 } }

// Big enough, and called often enough, to stay calls at -O2.
fn run(first: u8, n: i64, cap: i64) -> u8[..] {
    var l: u8[..] = [..cap];
    for i in n { l.push(first + i as u8); }
    return l;
}
fn wide(n: i64, step: i32) -> i32[..] {
    var l: i32[..] = [..8];
    var v: i32 = 1;
    for i in n {
        if l.len == l.cap { abort("full"); }
        l.push(v);
        v = v * step + i as i32;
    }
    return l;
}
fn small() -> u8[..] {
    var l: u8[..] = [..4];
    l.push('x');
    return l;
}
fn lbl(c: Shape.Circle) -> u8[..] { return run('c', c.r, 6); }
fn lbl(q: Shape.Sq) -> u8[..] { return run('s', q.s, 3); }
fn twice<F>(out: u8[>..]&) {
    out.append(F());
    out.push('/');
    out.append(F());
}
fn into(out: u8[>..<]&, n: i64) { out.append(run('r', n, 5)); }

fn main() {
    var g: u8[>..] = ['<'];
    g.append(run('a', 3, 8));
    g.push('|');
    g.append(small());
    g.push('|');
    g.append(run('z', 0, 4));
    g.push('>');
    print(g, " ", g.len);                       // <abc|x|> 8

    var gs: u8[>..<] = ['['];
    gs.append(run('d', 2, 2));
    gs.push(']');
    print(gs, " ", gs.len);                     // [de] 4
    gs.pop();
    gs.append(run('f', 2, 9));
    into(gs, 3);
    gs.push(']');
    print(gs, " ", gs.len);                     // [defgrst] 9

    var w: i32[>..] = [7];
    w.append(wide(3, 1000));
    w.push(9);
    w.append(wide(2, -1));
    w.append(wide(0, 5));
    print(w, " ", w.len);                       // [7, 1, 1000, 1000001, 9, 1, -1] 7

    var d: u8[>..] = [];
    for k in 2 {
        let sh: Shape = if k == 0 { Shape.Circle { r: 2 } } else { Shape.Sq { s: 1 } };
        d.append(lbl(sh));
        d.push('.');
    }
    print(d, " ", d.len);                       // cd.s. 5

    var fv: u8[>..] = ['{'];
    twice(fv) { run('m', 2, 4) }
    twice(fv, small);
    fv.push('}');
    print(fv, " ", fv.len);                     // {mn/mnx/x} 10

    var lim: u8[..] = [..6];
    lim.append(run('u', 2, 7));
    lim.push('!');
    print(lim, " ", lim.len, " ", lim.cap);     // uv! 3 6
}
