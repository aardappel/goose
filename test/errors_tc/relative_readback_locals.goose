// error: a relative reference must point within the same root as its location
// error: it was read out of s and may point into spare or pool
// s is a copy of an element of slots, bounded by it: two locals in scope at
// the container's block can hold a Node, so the read-back out of s names
// neither of them (§9.5).
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
        let s = slots[0];
        let n = s.n;
        assert(n);
        spare[0].next .= n;
    }
}
