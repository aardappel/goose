// error: a relative reference must point within the same root as its location
// error: call of make links a target rooted at pool to receiving storage n
struct Node { id: i64, me: Node&<u32 in pool> }
var pool: Node[>..] = [];
fn make() -> Node { Node { id: 1, me: self } }
fn main() {
    pool.push(make());
    let n = make();
    print(n.id);
}
