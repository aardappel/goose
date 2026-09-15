// error: .realloc_slice may move the slice
// error: contains self-relative references
// Growing a slice may move its elements, whose self-relative offsets would
// still measure from where they were (§3.9, §5.4); allocating and freeing
// never move them.
struct Node { v: i64 = 0, next: Node&<u16>? }
fn main() {
    reusable[] var pool: Node[>..] = [];
    let s = pool.alloc_slice(2);
    s[0].next .= s[1];
    pool.free_slice(s);
    let t = pool.realloc_slice(pool.alloc_slice(1), 3);
    print(t.len);
}
