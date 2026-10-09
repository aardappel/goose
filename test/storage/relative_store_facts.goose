// Stores into optional self-relative fields (§3.9) whose null encoding the
// compiler can rule out or must still check: a link that is not its
// struct's first field never encodes a target as offset zero, whatever it
// points at -- another element, a part of one, its own holder -- and a
// plain `T&` value, a call's result or a branch of them, is never null.
// Each stored link reads back as the reference that was stored.

struct Node { value: i64, next: Node&<u16>?, prev: Node&<u16>? }
struct Item { v: i32 }
struct Slot { item: Item, link: Item&<u16>? }
struct Head { link: Head&<u16>?, tag: i64 }

var nodes: Node[>..];

fn add(value: i64) -> Node& { return nodes.push(Node { value: value }); }
fn pick(a: Node&, b: Node&, first: bool) -> Node& { return if first { a } else { b }; }

fn val(n: Node?) -> i64 {
    guard n else { return -1; }
    return n.value;
}
fn ival(i: Item?) -> i32 {
    guard i else { return -1; }
    return i.v;
}

fn chain_sum(start: Node&) -> i64 {
    var total = 0;
    var cur: Node? = start;
    var steps = 0;
    while steps < 8 {
        guard cur else { break; }
        total += cur.value;
        cur .= cur.next;
        steps++;
    }
    return total;
}

fn main() {
    let a .= add(1);
    let b .= add(10);
    let c .= add(100);
    a.next .= b;
    b.next .= add(1000);            // a call's result: never null
    b.prev .= pick(a, c, true);
    c.prev .= c;                    // its own holder, from a later field
    c.next .= a;
    print(chain_sum(a), " ", chain_sum(c), " ", val(b.prev), " ", val(c.prev));
    // A literal built in the pool: fields past the first one, from a call
    // and from a branch of plain references.
    let d .= nodes.push(Node { value: 5, next: pick(c, a, true), prev: if b.value > 1 { a } else { c } });
    print(val(d.next), " ", val(d.prev));
    // A link to a part of another element, and to a part of its own holder.
    var slots: Slot[>..] = [];
    slots.push(Slot { item: Item { 3 } });
    slots.push(Slot { item: Item { 4 } });
    slots[0].link .= slots[1].item;
    slots[1].link .= slots[1].item;
    print(ival(slots[0].link), " ", ival(slots[1].link));
    // A first field is still checked, and holds anything but its holder.
    var heads: Head[>..] = [];
    heads.push(Head { tag: 1 });
    heads.push(Head { tag: 2 });
    heads[0].link .= heads[1];
    let o: Head? = null;
    heads[1].link .= o;
    let h = heads[0].link;
    guard h else { return; }
    print(h.tag, " ", heads[1].link == null);
}
