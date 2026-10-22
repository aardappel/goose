// error: self cannot initialize Node&<u32 in pool> in a .resize fill value, which is built once and copied into every slot the resize adds
// `self` in an `in pool` field is the value's own position in the pool,
// which the one fill value a resize copies into each slot it adds has not
// got (§3.9).
struct Node { next: Node&<u32 in pool>, v: i64 }
var pool: Node[>..] = [];
fn main() {
    pool.push(Node { next: self, v: 10 });
    pool.resize(3, Node { next: self, v: 7 });
    print(pool.index_of(pool[2].next));
}
