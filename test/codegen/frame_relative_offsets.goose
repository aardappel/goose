// A frame object's header and tail have one Goose root. A link in the
// tail can point into either allocation. The C emission test checks
// set_narrow's guard independently of where the two allocations land.
struct Entry { value: i32, wide: (i32&<u64>?)[2], narrow: i32&<u32>? }
struct Box { head: i32, entries: Entry[>..] }

fn set_narrow(b: Box&, header: bool) {
    b.entries[0].narrow .= if header { b.head } else { b.entries[0].value };
}

fn main() {
    var b = Box { head: 17, entries: [Entry { value: 42, wide: [null, null] }] };
    b.entries[0].wide[0] .= b.head;
    b.entries[0].wide[1] .= b.entries[0].value;
    b.entries.push(Entry { value: 7, wide: [null, null] });
    b.entries[1].wide[0] .= b.head;
    b.entries[1].wide[1] .= b.entries[0].value;
    assert(b.entries[0].wide == b.entries[1].wide);
    for link in b.entries[0].wide {
        assert(link);
        print(link);
    }
    set_narrow(b, false);
    let n = b.entries[0].narrow;
    assert(n);
    print(n);
    b.entries[0].wide[0] .= null;
    assert(b.entries[0].wide != b.entries[1].wide);
}
