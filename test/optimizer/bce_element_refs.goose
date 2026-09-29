// runtime-debug: a reference to an array element measures the slice or array the element holds, which writes and resizes of the element change.
struct Row { s: i64[:] }
struct Cell { v: i64[..4] }
recursive fn pop_first(p: i64[..4][:]) { p[0].pop(); p[0].pop(); }
recursive fn pop_first_ref(p: i64[..4][:]&) { p[0].pop(); p[0].pop(); }

// No element is a resizable array.
recursive fn store_keeps(ss: i64[:][:], q: i64[>..]&) {
    if q.len > 0 && ss.len > 0 {
        ss[0] = q[0..1];
        print(q[q.len - 1]); // bce:elide
    }
}

// Each element below holds one value when it is indexed, while the length
// it had before (n) says three: the checks stay, and pass.
fn slice_elements() {
    var b: i64[>..] = [1, 2, 3];
    var arr: i64[:][>..] = [b[..], b[..]];
    let r = &arr[0];
    var n = r.len;
    arr[0] = b[0..1];
    if n > 0 { print(r[0]); } // bce:keep
    let q: i64[:]& = arr[1];
    n = q.len;
    arr[1] = b[0..1];
    if n > 0 { print(q[0]); } // bce:keep
}

fn limited_elements() {
    var a: i64[..4][>..] = [];
    var x: i64[..4] = [1, 2, 3];
    a.push(x);
    a.push(x);
    let w = &a[0];
    var n = w.len;
    // A scalar stored into an element's element changes no length.
    a[0][1] = 7;
    if n > 1 { print(w[1]); } // bce:elide
    var y: i64[..4] = [5];
    a[0] = y;
    if n > 0 { print(w[0]); } // bce:keep
    let v = &a[1];
    n = v.len;
    a[1].pop();
    a[1].pop();
    if n > 0 { print(v[0]); } // bce:keep
}

fn element_fields() {
    var b: i64[>..] = [1, 2, 3];
    var rows: Row[>..] = [];
    rows.push(Row { s: b[..] });
    let r = &rows[0].s;
    var n = r.len;
    rows[0].s = b[0..1];
    if n > 0 { print(r[0]); } // bce:keep
    rows[0] = Row { s: b[..] };
    n = r.len;
    rows[0] = Row { s: b[0..1] };
    if n > 0 { print(r[0]); } // bce:keep
}

fn through_binders() {
    var a: i64[..4][>..] = [];
    var x: i64[..4] = [1, 2, 3];
    a.push(x);
    for &e in a {
        let n = e.len;
        a[0].pop();
        a[0].pop();
        if n > 0 { print(e[0]); } // bce:keep
    }
}

// A loop reads the view of an array it indexes through a reference once,
// before it starts, only if nothing in it can resize that array.
fn hoisted() {
    var a: i64[..4][>..] = [];
    var x: i64[..4] = [1, 2, 3];
    a.push(x);
    let w = &a[0];
    var i = 0;
    while i < w.len {
        print(w[i]);
        a[0].pop();
        i++;
    }
}

fn in_callees() {
    var a: i64[..4][>..] = [];
    var x: i64[..4] = [1, 2, 3];
    a.push(x);
    a.push(x);
    let w = &a[0];
    var n = w.len;
    pop_first(a[..]);
    if n > 0 { print(w[0]); } // bce:keep
    var s = a[1..];
    let v = &a[1];
    n = v.len;
    pop_first(s);
    if n > 0 { print(v[0]); } // bce:keep
    a[1] = x;
    n = v.len;
    let r = &s;
    pop_first_ref(r);
    if n > 0 { print(v[0]); } // bce:keep
}

// Through a reference an element holds, the store lands wherever that
// reference points.
fn through_stored_refs() {
    var c = Cell { v: [1, 2, 3] };
    var cells: Cell&[>..] = [];
    cells.push(&c);
    let n = c.v.len;
    var y: i64[..4] = [5];
    cells[0].v = y;
    if n > 0 { print(c.v[0]); } // bce:keep
}

fn main() {
    var q: i64[>..] = [4, 5, 6];
    var ss: i64[:][>..] = [q[..]];
    store_keeps(ss[..], q);
    slice_elements();
    limited_elements();
    element_fields();
    through_binders();
    hoisted();
    in_callees();
    through_stored_refs();
    var a: i64[..4][>..] = [];
    var x: i64[..4] = [1, 2, 3];
    a.push(x);
    let w = &a[0];
    let n = w.len;
    a[0].pop();
    a[0].pop();
    if n > 2 { print(w[2]); } // bce:keep
}
