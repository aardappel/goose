// A function growing an array through a reference parameter keeps the
// stack's top in a local (implementation notes §6.10). Around a call it
// writes the local back and reads it again only where the callee can reach
// that stack: a callee that mentions no global and captures nothing cannot,
// whatever stacks of its own it builds on, while one that pushes to a
// global, which the parameter may be, or captures a variable, still has the
// top in memory around it.

var gbuf: u8[>..] = [];

// Pure: no stacks, no globals.
fn shifted(c: u8, k: u8) -> u8 { return c + k; }

// Builds a string of its own on a data stack above its caller's.
fn digits(n: i64) -> i64 {
    let s = str(n, "/", n * n);
    return s.len;
}

// Reaches the array its caller may be growing.
fn note() { gbuf.push('!'); }

fn fill(out: u8[>..]&, n: i64) {
    for i in n {
        out.push(shifted('a', i as! u8));
        out.push('0' + digits(i * 7) as! u8);
    }
}

fn fill_noting(out: u8[>..]&, n: i64) {
    for i in n {
        out.push(shifted('A', i as! u8));
        note();
    }
}

fn main() {
    var local: u8[>..] = [];
    fill(local, 4);
    print(local);                // a3b4c6d6
    fill(gbuf, 2);
    print(gbuf);                 // a3b4
    gbuf.clear();
    fill_noting(gbuf, 3);
    print(gbuf);                 // A!B!C!
    var count = 0;
    fn bump(c: u8) -> u8 { count += 1; return c; }
    fn fill_counting(out: u8[>..]&) {
        for i in 3 { out.push(bump('x')); out.push('0' + count as! u8); }
    }
    var more: u8[>..] = [];
    fill_counting(more);
    print(more, " ", count);     // x1x2x3 3
}
