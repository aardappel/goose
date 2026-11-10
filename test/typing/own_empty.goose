// An own optional reference or slice known to hold nothing owes nothing
// (docs/design/ownership.md 4.6): one written as null or [], one a take()
// emptied, and one a condition found null. It reads as the empty value it
// is, and may be given a value again.
struct Node { v: i64, next: own Node? }
var nodes: Node[>..] = [];
var live = 0;
trusted fn make(v: i64, next: own Node?) -> own Node& {
    live += 1;
    return nodes.push(Node { v: v, next: next });
}
trusted fn free_node<F>(h: own Node&) { F(h); live -= 1; }

// A list is freed head first, each node handing over the one it owns: the
// loop leaves with cur found null, which is nothing left to consume.
fn free_list(h: own Node?) {
    var cur: own Node? = h;
    while cur {
        var rest: own Node? = null;
        free_node(cur) { rest .= take(it.next); };
        cur .= rest;
    }
}

fn never_filled() {
    var h: own Node? = null;
    print("never ", h == null);
}

fn filled_on_one_path(c: bool) {
    var h: own Node? = null;
    if c { h .= make(1, null); }
    // Beside the path that filled it, it may hold a value: what takes it finds out.
    free_list(take(h));
    print("one path ", live);
}

fn taken() {
    var h: own Node? = make(2, make(3, null));
    let got = take(h);                 // h is left null
    print("taken ", h == null, " ", live);
    h .= got;
    free_list(take(h));
    print("freed ", live);
}

fn found_null(h: own Node?) {
    if !h { print("null"); return; }   // nothing to consume on this path
    print("some ", h.v);
    free_list(h);
}

fn main() {
    never_filled();
    filled_on_one_path(true);
    filled_on_one_path(false);
    taken();
    found_null(null);
    found_null(make(4, null));
    print("live ", live);
}
