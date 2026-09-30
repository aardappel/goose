// A variable holding a copy out of the storage a parameter's class stands
// for holds views that storage holds, and says so to what reads it: a merge
// or a literal of the variable, a copy of it, a view read out of any of
// them, and a read in a loop that has the variable declared outside it,
// whose later stores the loop feeds back. A shrink in the callee judges them
// by what the callee stored there, and leaves the rest to each call, which
// judges it by its record of the argument's storage (§5.1). Here none of
// them views the array shrunk.
struct H { s: const u8[:] }
struct Q { h: H, n: i64 }

// Merges and literals of variables.
fn merged_vars(out: u8[>..]&, hs: H[:], c: bool) {
    let h1 = hs[0];
    let h2 = hs[1];
    let h = if c { h1 } else { h2 };
    out.clear();
    out.append(h.s);
}
fn merged_var_copy(out: u8[>..]&, hs: H[:], c: bool) {
    let h1 = hs[0];
    let h = if c { h1 } else { hs[1] };
    let s = h.s;
    out.clear();
    out.append(s);
}
fn literal_of_var(out: u8[>..]&, hs: H[:]) {
    let h = hs[1];
    let q = Q { h: h, n: 1 };
    out.clear();
    out.append(q.h.s);
}
fn view_of_literal_of_var(out: u8[>..]&, hs: H[:]) {
    let h = hs[0];
    let q = Q { h: h, n: 2 };
    let s = q.h.s;
    out.clear();
    out.append(s);
}
fn copy_of_var(out: u8[>..]&, hs: H[:]) {
    let h = hs[1];
    let g = h;
    let s = g.s;
    out.clear();
    out.append(s);
}
// A by-value binder's copy, merged and in a literal.
fn binder_merged(out: u8[>..]&, hs: H[:], c: bool) {
    for h in hs {
        let g = if c { h } else { hs[0] };
        out.clear();
        out.append(g.s);
        print(out);
    }
}
fn binder_literal(out: u8[>..]&, hs: H[:]) {
    for h in hs {
        let q = Q { h: h, n: 3 };
        let s = q.h.s;
        out.clear();
        out.append(s);
        print(out);
    }
}
// Copies stored by a callee and by a nested function.
fn put(h: H&, hs: H[:]) { h = hs[1]; }
fn stored_by_callee(out: u8[>..]&, hs: H[:]) {
    var h = H { s: "" };
    put(h, hs);
    let s = h.s;
    out.clear();
    out.append(s);
}
fn stored_by_nested(out: u8[>..]&, hs: H[:]) {
    var h = H { s: "" };
    fn keep(p: H[:]) { h = p[0]; }
    keep(hs);
    let s = h.s;
    out.clear();
    out.append(s);
}
// Reads in loops that have the variable declared outside them.
fn loop_let(out: u8[>..]&, hs: H[:]) {
    let h = hs[1];
    for i in 0..2 {
        let s = h.s;
        out.clear();
        out.append(s);
        print(out);
    }
}
fn loop_reassigned(out: u8[>..]&, hs: H[:]) {
    var h = hs[0];
    for i in 0..hs.len {
        let s = h.s;
        out.clear();
        out.append(s);
        print(out);
        if i == 0 { h = hs[i + 1]; } else { h = H { s: "text" }; }
    }
}
fn loop_nested(out: u8[>..]&, hs: H[:]) {
    let h = hs[0];
    var i = 0;
    while i < 2 {
        for j in 0..1 {
            let q = Q { h: h, n: i };
            let s = q.h.s;
            out.clear();
            out.append(s);
        }
        print(out, i);
        i++;
    }
}

fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "h0" });
    hs.push(H { s: "h1" });
    var other: u8[>..] = [];
    other.append("oo");
    var gs: H[>..] = [];
    gs.push(H { s: other[..] });
    gs.push(H { s: "g1" });

    merged_vars(buf, hs[..], true);
    print(buf);
    merged_vars(buf, gs[..], true);
    print(buf);
    merged_var_copy(buf, hs[..], false);
    print(buf);
    literal_of_var(buf, gs[..]);
    print(buf);
    view_of_literal_of_var(buf, gs[..]);
    print(buf);
    copy_of_var(buf, hs[..]);
    print(buf);
    binder_merged(buf, gs[..], true);
    binder_merged(buf, hs[..], false);
    binder_literal(buf, gs[..]);
    stored_by_callee(buf, gs[..]);
    print(buf);
    stored_by_nested(buf, gs[..]);
    print(buf);
    loop_let(buf, hs[..]);
    loop_reassigned(buf, gs[..]);
    loop_nested(buf, gs[..]);
}
