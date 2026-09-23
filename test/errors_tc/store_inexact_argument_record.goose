// error: cannot clear arr while a1 is still used: a reference into it was stored there
// f stores into what r names, which may be a1 as well as a2, the root the
// argument kept: the call puts the store on record for both (§5.1).
struct Node { p: i64? }

fn f(r: Node&, x: i64&) { r.p .= x; }

fn main() {
    var arr: i64[>..] = [1, 2, 3];
    var a1 = Node { p: null };
    var a2 = Node { p: null };
    var c = true;
    let r: Node& = if c { a2 } else { a1 };
    f(r, arr[0]);
    arr.clear();
    if a1.p { print(a1.p); }
}
