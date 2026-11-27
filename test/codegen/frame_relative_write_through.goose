// A write through the tail's link must be visible through the fixed field,
// both across a call and when the two accesses are in the same function.
struct Entry { pad0: i64, link: i64&<u64>? }
struct Box { head: i64, entries: Entry[>..] }

fn poke(b: Box&, v: i64) {
    let link = b.entries[0].link;
    assert(link);
    link = v;
}

fn main() {
    var b = Box { head: 5, entries: [Entry { pad0: 0, link: null }] };
    b.entries[0].link .= b.head;
    b.head = 7;
    poke(b, 99);
    print(b.head);
    let link = b.entries[0].link;
    assert(link);
    link = 42;
    print(b.head);
}
