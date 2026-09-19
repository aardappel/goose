// A queue read's runtime-capacity limited result appended to a grow-only
// and a grow-shrink array (§3.3, §11.2): only its elements join the run, as
// for the calls in append_limited_call.goose. A program of its own, since
// TinyCC runs none that uses the queues.
fn run(first: u8, n: i64, cap: i64) -> u8[..] {
    var l: u8[..] = [..cap];
    for i in n { l.push(first + i as u8); }
    return l;
}

fn main() {
    qput(run('q', 2, 5));
    qput(run('t', 0, 3));
    qput(run('w', 3, 3));
    var qq: u8[>..] = ['('];
    qq.append(qget<u8[..]>());
    qq.append(qget<u8[..]>());
    qq.push(')');
    print(qq, " ", qq.len);                     // (qr) 4
    var gs: u8[>..<] = ['['];
    gs.append(qget<u8[..]>());
    gs.push(']');
    print(gs, " ", gs.len);                     // [wxy] 5
}
