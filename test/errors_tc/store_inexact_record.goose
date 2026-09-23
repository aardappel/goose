// error: cannot clear arr while a1 is still used: a reference into it was stored there
// The rebind through r may have stored into a1 as well as a2, the root it
// kept, so a1 holds a reference into arr as far as the shrink rule knows
// (§5.1, §9.2).
struct Node { p: i64? }

fn main() {
    var arr: i64[>..] = [1, 2, 3];
    var a1 = Node { p: null };
    var a2 = Node { p: null };
    var c = true;
    let r: Node& = if c { a2 } else { a1 };
    r.p .= arr[0];
    arr.clear();
    if a1.p { print(a1.p); }
}
