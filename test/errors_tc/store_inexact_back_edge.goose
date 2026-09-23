// error: cannot clear arr while a is still used: a reference into it was stored there
// The back edge passes q, which may be r's node a or the local loc, for s,
// which the recursive activation stores into; the call records it in both,
// so a holds a reference into arr after f returns (§5.1, §7.8).
struct Node { p: i64? }

recursive fn f(pool: i64[>..]&, r: Node&, s: Node&, n: i64) {
    s.p .= pool[0];
    if n > 0 {
        var loc = Node { p: null };
        let q: Node& = if n > 1 { r } else { loc };
        f(pool, q, q, n - 1);
    }
}

fn main() {
    var arr: i64[>..] = [1, 2, 3];
    var a = Node { p: null };
    var b = Node { p: null };
    f(arr, a, b, 2);
    arr.clear();
    if a.p { print(a.p); }
}
