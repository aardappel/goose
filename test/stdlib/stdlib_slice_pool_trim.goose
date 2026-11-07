// runtime-debug: trim gives back the pages of free runs, including the edges merged runs shared, and keeps live runs.
import slice_pool;

fn fill(s: u8[:], v: u8) { for i in s.len { s[i] = v; } }

fn all(s: u8[:], v: u8) -> bool {
    for b in s { if b != v { return false; } }
    return true;
}

// Two live runs around two freed ones that merge. Sizes are no page multiple.
fn neighbours() {
    var p = slice_pool<u8> {};
    assert(p.trim());                            // no tree yet
    let a = p.alloc_slice(10007);
    let b = p.alloc_slice(300001);
    let c = p.alloc_slice(300003);
    let d = p.alloc_slice(10009);
    fill(a, 1); fill(b, 2); fill(c, 3); fill(d, 4);
    assert(p.trim());                            // nothing free: nothing changes
    assert(all(a, 1) && all(b, 2) && all(c, 3) && all(d, 4));
    p.free_slice(b);
    p.free_slice(c);
    assert(p.trim() && p.trim());                // a repeat is harmless
    assert(all(a, 1) && all(d, 4));              // live runs keep every byte
    // The old edge between b and c is deep inside the merged span, so its page
    // went back as well; a stale view reads zeros there and in the middles.
    assert(b[b.len - 1] == 0 && c[0] == 0 && b[b.len / 2] == 0 && c[c.len / 2] == 0);
    let e = p.alloc_slice(600004);               // reuse: the whole merged span
    assert(p.items.range_index(e) == 10007 && all(e, 0));
    fill(e, 5);
    assert(all(e, 5) && all(a, 1) && all(d, 4));
}

// Enough free spans for an inner level, so the walk visits every leaf.
fn many_spans() {
    var p = slice_pool<u8> {};
    var live: u8[:][>..] = [];
    var freed: u8[:][>..] = [];
    for i in 100 {
        let l = p.alloc_slice(1000);
        let f = p.alloc_slice(140000);
        fill(l, 7); fill(f, 8);
        live.push(l);
        freed.push(f);
    }
    for f in freed { p.free_slice(f); }
    assert(p.tree.height > 0);
    assert(p.trim());
    for l in live { assert(all(l, 7)); }
    for f in freed { assert(f[f.len / 2] == 0); }
}

fn main() {
    neighbours();
    many_spans();
    print("slice pool trim: ok");
}
