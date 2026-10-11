// A grow-only array is assigned whole through a reference wherever a clear
// of the same location would be accepted (§4.4, §5.1): a struct's tail field
// through a reference parameter, from [], a copy or a call's result; the
// struct whole; the array whole through a reference to it; a nested tail and
// the frame object holding it; a struct with a variable-size prefix; and the
// same for a grow-shrink tail (§5.2).
import std;

struct Stack { n: i64, items: i64[>..] }

fn count_to(n: i64) -> i64[>..] {
    var r: i64[>..] = [];
    for i in n { r.push(i); }
    return r;
}

fn reset(s: Stack&) { s.items = []; }
fn refill(s: Stack&, xs: i64[>..]&) { s.items = copy(xs); }
fn regen(s: Stack&) { s.items = count_to(s.n); }
fn fresh(s: Stack&) { s = Stack { n: 2, items: [7, 8] }; }
fn overwrite(a: i64[>..]&) { a = [5, 5, 5]; }

struct Outer { k: i64, s: Stack }

fn reset_inner(o: Outer&) { o.s.items = [o.k]; }
fn fresh_inner(o: Outer&) { o.s = Stack { n: 1, items: count_to(o.k) }; }

struct Program { commands: u8[], jumps: i64[>..] }

fn renumber(p: Program&) { p.jumps = count_to(p.commands.len); }

struct Queue { n: i64, items: i64[>..<] }

fn requeue(q: Queue&) { q.items = count_to(q.n); }

fn main() {
    var s = Stack { n: 3, items: [1, 2] };
    reset(s);
    print(s.items.len);
    var src: i64[>..] = [4, 5, 6];
    refill(s, src);
    print(s.items);
    regen(s);
    print(s.items);
    fresh(s);
    print(s.n, " ", s.items);
    overwrite(s.items);
    print(s.items);
    // A slice of the array is fine once it is no longer used.
    let v = s.items[1..];
    print(v);
    for i in 3 {
        regen(s);
        s.items.push(i);
    }
    print(s.items);

    var o = Outer { k: 4, s: Stack { n: 0, items: [9, 9] } };
    reset_inner(o);
    print(o.s.items);
    fresh_inner(o);
    print(o.s.n, " ", o.s.items);

    var p = Program { commands: [1, 2, 3], jumps: [] };
    renumber(p);
    print(p.jumps);

    var q = Queue { n: 2, items: [8] };
    requeue(q);
    print(q.items);
}
