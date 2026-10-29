// runtime-debug: default-constructed self-relative elements stay at their addresses.
import slice_pool;
struct Link { value: i64 = 0, next: Link&<u32>? }
fn main() {
    var p = slice_pool<Link> {};
    let s = p.alloc_slice(3);
    s[0].next .= s[2];
    s[2].value = 7;
    let next = s[0].next;
    if next { assert(next.value == 7); } else { assert(false); }
    p.free_slice(s);
    let again = p.alloc_slice(3);
    assert(again[0].next == null && s[2].value == 0);
    print("relative slices");
}
