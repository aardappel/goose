// A reusable pool renders as any resizable does (§3.7, §5.4): a user format
// overload taking it by reference is handed the pool -- a pool reference
// where the overload's specialization is keyed to a pool, as for any call
// -- whether print, str or format renders it, and whether it is a global,
// a local or what a pool reference parameter names; format renders into a
// pool it is given an aggregate's text, and the parts a user overload
// renders, through a pool reference to it.
struct S { x: i64 }

fn format(out: u8[>..]&, v: i32[>..]&) {
    format(out, "pool of ", v.len, " first ", v[0]);
}
fn format(out: u8[>..]&, s: S) {
    format(out, "S", s.x);
}

reusable var gp: i32[>..] = [];
reusable var text: u8[>..] = [];

fn show(p: i32[>..]&) { print(p); }

fn main() {
    gp.push(1);
    print(gp);
    reusable var lp: i32[>..] = [];
    lp.push(2);
    lp.push(3);
    print(lp);
    print(str(lp));
    var out: u8[>..] = [];
    format(out, lp);
    print(out);
    show(lp);
    show(gp);
    format(text, S { x: 5 }, " ", [1, 2], " ", gp);
    print(text);
}
