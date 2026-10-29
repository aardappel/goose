// runtime-debug all-levels
import slice_pool;
fn main() {
    var p = slice_pool<i64> {};
    p.recycle_all();
    let kept = p.alloc_slice(10000);
    kept[10] = 42;
    for i in 2000 { p.free_slice(kept[i * 4..i * 4 + 2]); }
    let count = p.nodes.len;
    p.recycle_all(); p.recycle_all();
    assert(p.items.len == 10000 && p.nodes.len == count && kept[10] == 42);
    let all = p.alloc_slice(10000);
    assert(p.items.range_index(all) == 0 && all[10] == 0 && p.items.len == 10000);
    for i in 2000 { p.free_slice(all[i * 4..i * 4 + 2]); }
    assert(p.nodes.len == count);
    p.recycle_all();
    assert(p.alloc_slice(10001).len == 10001 && p.items.len == 10001);
    print("slice pool recycle: ok");
}
