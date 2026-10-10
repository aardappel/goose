// error: a relative reference must point within the same root as its location
// error: it was read out of s and may point into pool or spare
// s is a copy of an element of slots, which holds a reference into pool and
// one into spare, so the read-back out of s names neither of them (§9.5).
struct Node { key: i32, next: Node&<u32>? }
struct S { n: Node&? }
fn main() {
    var pool: Node[>..] = [];
    pool.push(Node { key: 1 });
    block {
        var spare: Node[>..] = [];
        var slots: S[>..] = [];
        spare.push(Node { key: 9 });
        slots.push(S { n: pool[0] });
        slots.push(S { n: spare[0] });
        let s = slots[0];
        let n = s.n;
        assert(n);
        spare[0].next .= n;
    }
}
