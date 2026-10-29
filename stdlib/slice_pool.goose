// First-fit runs with coalescing and stable backing storage. Freeing marks a
// run reusable; existing views remain valid and can observe replacement values.
// Elements must be fixed-size. alloc_slice/realloc_slice require defaults;
// realloc_slice/alloc_copy require normally copyable values.
// Treat nodes/tree as bookkeeping and do not shrink or replace items directly.

import slice_pool_internal;

root struct slice_pool<T> {
    items: T[>..]                       = [],
    nodes: slice_pool_detail::node[>..] = [],
    tree:  slice_pool_detail::tree      = slice_pool_detail::tree { .. },
}
inline fn slice_pool_detail::defaults<T>(p: slice_pool<T>&, first: i64, end: i64) {
    let used_end = if end < p.items.len { end } else { p.items.len };
    let reused = p.items[first..used_end];
    for i in reused.len { reused[i] .= default<T>(); }
    for i in p.items.len..end { p.items.push(default<T>()); }
}

// Copy before defaulting, with memmove ordering even when a freed prefix lets
// the new run overlap the old one. New array elements are always initialized.
fn slice_pool_detail::finish<T>(p: slice_pool<T>&, idx: i64, n: i64, old_idx: i64, old: T[:]) -> T[:] {
    if idx < 0 || idx > p.items.len { abort("slice_pool.realloc_slice: span tree is corrupt"); }
    let end = p.items.check_length(idx + n, "slice_pool.realloc_slice: pool would outgrow its data stack");
    if old.len != 0 && idx != old_idx {
        let available = p.items.len - idx;
        var existing = old.len;
        if existing > available { existing = available; }
        // The destination tail is past all source elements, so it can be
        // appended before moving the overlapping existing prefix backward.
        p.items.append(old[existing..]);
        p.items[idx..idx + existing].move_from(old[..existing]);
    }
    slice_pool_detail::defaults(p, idx + old.len, end);
    return p.items[idx..end];
}

inline fn alloc_slice<T>(p: slice_pool<T>&, n: i64) -> T[:] {
    let count = p.items.check_length(n, "slice_pool.alloc_slice: invalid slice length");
    let idx = if count == 0 || p.nodes.len == 0 { p.items.len } else { slice_pool_detail::place(p.nodes, p.tree, p.items.len, count) };
    if idx < 0 || idx > p.items.len { abort("slice_pool.alloc_slice: span tree is corrupt"); }
    let end = p.items.check_length(idx + count, "slice_pool.alloc_slice: pool would outgrow its data stack");
    slice_pool_detail::defaults(p, idx, end);
    return p.items[idx..end];
}
fn free_slice<T>(p: slice_pool<T>&, s: T[:]) {
    let idx = p.items.range_index(s, "slice_pool.free_slice: slice is not from this pool");
    slice_pool_detail::release_run(p.nodes, p.tree, idx, s.len);
}

// Initialize directly from source values, including overlapping views of items.
inline fn alloc_copy<T>(p: slice_pool<T>&, src: T[:]) -> T[:] {
    let count = p.items.check_length(src.len, "slice_pool.alloc_copy: invalid slice length");
    let idx = if count == 0 || p.nodes.len == 0 { p.items.len } else { slice_pool_detail::place(p.nodes, p.tree, p.items.len, count) };
    if idx < 0 || idx > p.items.len { abort("slice_pool.alloc_copy: span tree is corrupt"); }
    let end = p.items.check_length(idx + count, "slice_pool.alloc_copy: pool would outgrow its data stack");
    let available = p.items.len - idx;
    let existing = if count < available { count } else { available };
    // Append beyond all source elements before overwriting a shared prefix.
    if existing < count { p.items.append(src[existing..]); }
    if existing != 0 { p.items[idx..idx + existing].move_from(src[..existing]); }
    return p.items[idx..end];
}
fn realloc_slice<T>(p: slice_pool<T>&, s: T[:], n: i64) -> T[:] {
    let count = p.items.check_length(n, "slice_pool.realloc_slice: invalid slice length");
    let idx = p.items.range_index(s, "slice_pool.realloc_slice: slice is not from this pool");
    if count <= s.len {
        slice_pool_detail::release_run(p.nodes, p.tree, idx + count, s.len - count);
        return p.items[idx..idx + count];
    }
    var to = idx;
    if s.len == 0 { to = slice_pool_detail::place(p.nodes, p.tree, p.items.len, count); }
    else if idx + s.len != p.items.len { to = slice_pool_detail::regrow(p.nodes, p.tree, p.items.len, idx, s.len, count); }
    return slice_pool_detail::finish(p, to, count, idx, s);
}

// Retain values and backing storage; every element belongs to one free run.
fn recycle_all<T>(p: slice_pool<T>&) {
    if p.nodes.len == 0 && p.items.len == 0 { return; }
    while p.nodes.len < 2 { p.nodes.push(slice_pool_detail::node { .. }); }
    p.tree = slice_pool_detail::tree { root: 1, height: 0, chain: 0 };
    p.nodes[1] = slice_pool_detail::node { .. };
    if p.items.len != 0 {
        p.nodes[1].n = 1;
        p.nodes[1].data[31] = p.items.len;
    }
    for i in 2..p.nodes.len {
        p.nodes[i].n = 0;
        p.nodes[i].link = p.tree.chain;
        p.tree.chain = i;
    }
}
