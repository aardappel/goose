// A relative link may own the slot it names (docs/design/ownership.md 4.6):
// a 4-byte offset in place of an own reference, from the base of a named
// pool or from the link itself. take() loads the reference the link stands
// for and leaves the null offset, replace() stores one and gives back the
// other, and a plain read walks the links as a borrow.
import slot_pool;
struct Node { key: i64, next: own Node&<u32 in nodes.items>? }
struct Chain { v: i64, next: own Chain&<u32>? }   // self-relative: built in its slot
var nodes = slot_pool<Node> {};
var chains = slot_pool<Chain> {};

fn push(head: own Node?, k: i64) -> own Node& { nodes.take(Node { key: k, next: head }) }

fn free_list(h: own Node?) {
    var cur = h;
    while cur {
        var rest: own Node? = null;
        nodes.release(cur) { rest .= take(it.next); };
        cur .= rest;
    }
}

// Releases the node after n, whose successor takes its place.
fn drop_after(n: Node&) {
    let gone = take(n.next);
    if gone {
        var rest: own Node? = null;
        nodes.release(gone) { rest .= take(it.next); };
        n.next .= rest;
    }
}

fn set_next(n: Node&, nx: own Node?) -> own Node? { replace(n.next, nx) }

fn show(what: u8[:], h: Node?) {
    var line: u8[>..] = [];
    var cur = h;
    while cur { format(line, " ", cur.key); cur .= cur.next; }
    print(what, line, " live ", nodes.items.len - nodes.free_slots.len);
}

fn main() {
    var head: own Node? = null;
    for k in 5 { head .= push(take(head), k); }
    show("built", head);
    if head { drop_after(head); }
    show("dropped", head);
    if head {
        let tail = set_next(head, null);
        show("cut", head);
        free_list(tail);
    }
    show("tail freed", head);
    free_list(take(head));
    print("live ", nodes.items.len - nodes.free_slots.len, " of ", nodes.items.len);

    var c: own Chain? = null;
    for k in 3 { c .= chains.take_with() { Chain { v: k, next: take(c) } }; }
    var sum = 0;
    while c {
        sum += c.v;
        var rest: own Chain? = null;
        chains.release(c) { rest .= take(it.next); };
        c .= rest;
    }
    print("chain ", sum, " live ", chains.items.len - chains.free_slots.len);
}
