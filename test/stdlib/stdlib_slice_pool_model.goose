// runtime-debug all-levels: short runs of a byte pool allocated, freed and resized many times over, as a cache of strings does, with free spans enough for a tree of several levels: every placement is the one a plain model of §5.4 makes.
// The model keeps the free spans in index order in two arrays. A run goes at
// the front of the first span that holds it, else at the end of the pool,
// starting with a free span that reaches the end; freed elements merge with
// the spans they touch; growth stays in place where a free span after the
// slice holds the difference or reaches the end. Each live run is filled with
// a byte of its own, so that runs the pool handed out twice would show.

import slice_pool;

var seed: u64 = 99;

fn next(n: i64) -> i64 {
    seed = seed * 6364136223846793005 + 1442695040888963407;
    return ((seed >> 33) % n as! u64) as! i64;
}

// The model: the free spans, and the pool's length.
var starts: i64[..4096] = [];
var counts: i64[..4096] = [];
var size: i64 = 0;

fn insert_span(k: i64, at: i64, n: i64) {
    starts.push(0);
    counts.push(0);
    var j = starts.len - 1;
    while j > k {
        starts[j] = starts[j - 1];
        counts[j] = counts[j - 1];
        j--;
    }
    starts[k] = at;
    counts[k] = n;
}

fn remove_span(k: i64) {
    var j = k;
    while j + 1 < starts.len {
        starts[j] = starts[j + 1];
        counts[j] = counts[j + 1];
        j++;
    }
    starts.pop();
    counts.pop();
}

// n elements off the front of span k, all of it where it holds no more.
fn take_span(k: i64, n: i64) {
    if counts[k] <= n {
        remove_span(k);
    } else {
        starts[k] += n;
        counts[k] -= n;
    }
}

fn model_alloc(n: i64) -> i64 {
    for k in starts.len {
        if counts[k] >= n {
            let at = starts[k];
            take_span(k, n);
            return at;
        }
    }
    let last = starts.len - 1;
    var at = size;
    if last >= 0 && starts[last] + counts[last] == size {
        at = starts[last];
        remove_span(last);
    }
    size = at + n;
    return at;
}

// How many spans start at or before i.
fn spans_upto(i: i64) -> i64 {
    var k = 0;
    while k < starts.len && starts[k] <= i { k++; }
    return k;
}

fn model_free(i: i64, n: i64) {
    if n == 0 { return; }
    let k = spans_upto(i);
    let prev = k > 0 && starts[k - 1] + counts[k - 1] == i;
    let next = k < starts.len && starts[k] == i + n;
    if prev && next {
        counts[k - 1] += n + counts[k];
        remove_span(k);
    } else if prev {
        counts[k - 1] += n;
    } else if next {
        starts[k] = i;
        counts[k] += n;
    } else {
        insert_span(k, i, n);
    }
}

// realloc_slice of the run [i, i + ol) to n elements: where it starts now.
fn model_realloc(i: i64, ol: i64, n: i64) -> i64 {
    if n <= ol {
        model_free(i + n, ol - n);
        return i;
    }
    if i + ol == size {
        size = i + n;
        return i;
    }
    let k = spans_upto(i + ol) - 1;
    if k >= 0 && starts[k] == i + ol && (counts[k] >= n - ol || starts[k] + counts[k] == size) {
        if starts[k] + counts[k] == size && counts[k] < n - ol { size = i + n; }
        take_span(k, n - ol);
        return i;
    }
    model_free(i, ol);
    return model_alloc(n);
}

// Mostly the lengths of short strings, now and then shorter or longer.
fn length() -> i64 {
    let r = next(20);
    if r == 0 { return 1 + next(4); }
    if r == 1 { return 15 + next(50); }
    return 5 + next(10);
}

// Check exact subtree maxima as well as placements: stale overestimates can
// leave ordinary placements unchanged until a later allocation needs them.
recursive fn validate_node(pool: slice_pool<u8>&, id: i64, height: i64, span: i64&) -> i64 {
    let node .= pool.nodes[id];
    assert(node.n >= 0 && node.n <= (if height == 0 { 31 } else { 20 }));
    var maximum = 0;
    for k in node.n {
        var count = 0;
        if height == 0 {
            assert(span < starts.len);
            assert(node.data[k] == starts[span] && node.data[31 + k] == counts[span]);
            count = node.data[31 + k];
            span++;
        } else {
            let child = node.data[40 + k];
            assert(pool.nodes[child].n > 0 && node.data[k] == pool.nodes[child].data[0]);
            count = validate_node(pool, child, height - 1, span);
            assert(node.data[20 + k] == count);
        }
        if count > maximum { maximum = count; }
    }
    return maximum;
}

fn validate(pool: slice_pool<u8>&) {
    assert(pool.items.len == size);
    if pool.nodes.len == 0 { assert(starts.len == 0); return; }
    assert(pool.tree.height >= 0 && pool.tree.height < 24);
    var span = 0;
    validate_node(pool, pool.tree.root, pool.tree.height, span);
    assert(span == starts.len);
}

fn main() {
    var pool = slice_pool<u8> {};
    let slots = 2000;
    var runs: (u8[:])[>..] = [];
    var tags: u8[>..] = [];
    for q in slots {
        runs.push(pool.alloc_slice(0));
        tags.push(0);
    }
    var placed = 0;
    var wrong = 0;
    var deepest = 0;
    for step in 60000 {
        let q = next(slots);
        let s = runs[q];
        let what = next(10);
        if s.len == 0 {
            let n = length();
            let t = pool.alloc_slice(n);
            let at = model_alloc(n);
            placed++;
            if pool.items.index_of(t[0]) != at { wrong++; }
            let tag = (1 + next(250)) as! u8;
            for j in n { t[j] = tag; }
            runs[q] = t;
            tags[q] = tag;
        } else if what < 4 {
            // all of it back
            model_free(pool.items.index_of(s[0]), s.len);
            pool.free_slice(s);
            runs[q] = s[..0];
        } else if what < 5 {
            // the front part back
            let k = 1 + next(s.len);
            model_free(pool.items.index_of(s[0]), k);
            pool.free_slice(s[..k]);
            runs[q] = s[k..];
        } else if what < 6 && s.len >= 3 {
            // a middle part back, then the back part, which merges with it
            let a = 1 + next(s.len - 2);
            let b = a + 1 + next(s.len - a - 1);
            let i = pool.items.index_of(s[0]);
            model_free(i + a, b - a);
            pool.free_slice(s[a..b]);
            model_free(i + b, s.len - b);
            pool.free_slice(s[b..]);
            runs[q] = s[..a];
        } else {
            // resized: shrunk, grown by a little, or doubled
            let ol = s.len;
            let r = next(3);
            let n = if r == 0 { next(ol) } else if r == 1 { ol + 1 + next(4) } else { ol * 2 };
            let i = pool.items.index_of(s[0]);
            let t = pool.realloc_slice(s, n);
            let at = model_realloc(i, ol, n);
            if n > 0 {
                placed++;
                if pool.items.index_of(t[0]) != at { wrong++; }
            }
            for j in n { t[j] = tags[q]; }
            runs[q] = t;
        }
        if starts.len > deepest { deepest = starts.len; }
        if step % 257 == 0 { validate(pool); }
    }
    validate(pool);
    print("placements ", placed, ", not where the model put them ", wrong);
    print("pool ", pool.items.len, " model ", size, ", at most ", deepest, " free spans at once");

    // Every run still holds its own byte.
    var bad = 0;
    for q in slots {
        for v in runs[q] { if v != tags[q] { bad++; } }
    }
    print("bytes overwritten: ", bad);

    // All of it back, from the last slot down: one span again.
    var slot = slots - 1;
    while slot >= 0 {
        pool.free_slice(runs[slot]);
        slot--;
    }
    let all = pool.alloc_slice(pool.items.len);
    print("one span: ", pool.items.index_of(all[0]), " ", all.len == pool.items.len);
}
