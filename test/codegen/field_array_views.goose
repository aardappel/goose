// Arrays held in fields, indexed in loops through a reference to the struct:
// codegen reads such an array's base and length once ahead of a loop that
// can neither resize it nor rebind the path to it (BCE's hoistfields), also
// where an inlined callee's parameter names it. Every loop here prints what
// it read, so a view kept past a push, a pop, a call that grows the array, a
// write of the whole array, a rebinding or a for loop's count that grows it
// would print stale values or fail a bounds check.

struct Rows { w: i64, rows: (i64[:])[>..] }
struct Cells { n: i64, cells: i64[>..] }
struct Outer { tag: i64, inner: Stack }
struct Stack { items: i64[>..<] }

fn cell(c: Cells&, i: i64) -> i64 { c.cells[i] }

fn grow(c: Cells&, v: i64) { c.cells.push(v); }

// Through a reference, read-only: the view is read once.
fn sum_cells(c: Cells&) -> i64 {
    var s = 0;
    for i in c.cells.len { s += c.cells[i] * (i + 1); }
    var j = 0;
    while j < c.cells.len {
        s += cell(c, j);   // Inlined: its parameter stands for c.
        j++;
    }
    return s;
}

// Rows of a slice pool, indexed twice deep as a maze's cells are.
fn sum_rows(r: Rows&) -> i64 {
    var s = 0;
    for y in r.rows.len {
        for x in r.w { s += r.rows[y][x] * (y + 2); }
    }
    return s;
}

// The array grows inside the loop, directly and through a call.
fn grow_inside(c: Cells&) -> i64 {
    var s = 0;
    var i = 0;
    while i < c.cells.len {
        s += c.cells[i];
        if c.cells[i] % 3 == 0 && c.cells.len < 12 { c.cells.push(c.cells[i] + 1); }
        if c.cells[i] % 5 == 0 && c.cells.len < 12 { grow(c, 50 + i); }
        i++;
    }
    return s * 100 + c.cells.len;
}

// A grow-shrink array popped and pushed in the loop.
fn churn(st: Stack&) -> i64 {
    var s = 0;
    for k in 6 {
        s = s * 3 + st.items[st.items.len - 1];
        if k % 2 == 0 { st.items.pop(); } else { st.items.push(k * 10); }
    }
    return s;
}

// A nested field, replaced inside the loop.
fn outer_sum(o: Outer&, other: Outer&) -> i64 {
    var s = 0;
    for i in 4 {
        s = s * 10 + o.inner.items[i % o.inner.items.len];
        if i == 1 { o.inner.items = copy(other.inner.items); }
    }
    return s;
}

// A rebindable reference moved to another struct mid-loop.
fn rebind_sum(a: Cells&, b: Cells&) -> i64 {
    var r = a;
    var s = 0;
    for i in 4 {
        s = s * 10 + r.cells[i];
        if i == 1 { r .= b; }
    }
    return s;
}

// Out of line (`recursive`), so that the loops below call it.
recursive fn append(out: i64[>..]&, v: i64) { out.push(v); }

// A local array grows in the loop, through a call: no reference the caller
// handed in can name it, so the view of c.cells stays. This function and
// the next are out of line too, so that their parameters are references a
// caller handed in.
recursive fn collect(c: Cells&) -> i64 {
    var out: i64[>..] = [];
    for i in c.cells.len {
        append(out, c.cells[i] * 2);
        if c.cells[i] % 2 == 0 { append(out, c.cells[i]); }
    }
    return out.len * 1000 + out[out.len - 1];
}

// d grows in the loop. Called with c and d the same struct, c.cells grows
// with it; called with two structs, it does not.
recursive fn grow_other(c: Cells&, d: Cells&) -> i64 {
    var s = 0;
    var i = 0;
    while i < c.cells.len && i < 8 {
        s = s * 2 + c.cells[i];
        if i < 3 { d.cells.push(i + 20); }
        i++;
    }
    return s * 100 + c.cells.len;
}

// A for loop's count grows the arrays it then reads, through a reference
// and in a field: the views are read before the count is, so they must not
// be kept.
fn grow_to(c: Cells&, n: i64) -> i64 {
    while c.cells.len < n { c.cells.push(c.cells.len * 10); }
    return n;
}

fn grow_ref(a: i64[>..]&, n: i64) -> i64 {
    while a.len < n { a.push(a.len * 10); }
    return n;
}

recursive fn counted_sums(c: Cells&, a: i64[>..]&) -> i64 {
    var s = 0;
    for i in grow_to(c, 6) { s += c.cells[i]; }
    for i in grow_ref(a, 5) { s += a[i] * 1000; }
    return s;
}

// The same loops inlined into a function that is never inlined itself
// (`recursive`), where they read through its reference parameters.
recursive fn report(r: Rows&, c: Cells&, st: Stack&, o: Outer&, p: Outer&, a: Cells&, b: Cells&) {
    print(sum_cells(c), " ", sum_rows(r));
    print(grow_inside(c));
    print(c.cells);
    print(churn(st), " ", st.items);
    print(outer_sum(o, p));
    print(rebind_sum(a, b));
}

fn main() {
    reusable[] var pool: i64[>..] = [];
    var r = Rows { w: 3, rows: [] };
    for y in 2 {
        let row = pool.alloc_slice(3);
        for x in 3 { row[x] = y * 3 + x; }
        r.rows.push(row);
    }
    var c = Cells { n: 0, cells: [1, 2, 3, 5] };
    print(sum_cells(c), " ", sum_rows(r));
    print(grow_inside(c));
    print(c.cells);

    var st = Stack { items: [1, 2, 3] };
    print(churn(st), " ", st.items);

    var o = Outer { tag: 1, inner: Stack { items: [1, 2, 3] } };
    var p = Outer { tag: 2, inner: Stack { items: [7, 8, 9] } };
    print(outer_sum(o, p));

    var a = Cells { n: 0, cells: [1, 2, 3, 4] };
    var b = Cells { n: 0, cells: [5, 6, 7, 8] };
    print(rebind_sum(a, b));

    print(collect(c));
    var e = Cells { n: 0, cells: [1, 2] };
    var f = Cells { n: 0, cells: [3, 4] };
    print(grow_other(e, e), " ", e.cells);
    print(grow_other(f, e), " ", f.cells, " ", e.cells.len);

    var g = Cells { n: 0, cells: [1, 2, 3] };
    var ga: i64[>..] = [4, 5];
    print(counted_sums(g, ga));

    var c2 = Cells { n: 0, cells: [1, 2, 3, 10] };
    var st2 = Stack { items: [1, 2, 4] };
    var o2 = Outer { tag: 1, inner: Stack { items: [1, 2, 4] } };
    var a2 = Cells { n: 0, cells: [1, 2, 3, 5] };
    r.w = 2;
    report(r, c2, st2, o2, p, a2, b);
}
