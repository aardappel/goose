// error: self cannot initialize Node&<u32 in pool> in a .resize fill value, which is built once and copied into every slot the resize adds
// A branch's value, and a literal inside the fill value's, are the fill
// value all the same: copied, not built in each slot (§3.9).
struct Node { next: Node&<u32 in pool>, v: i64 }
struct Pair { a: Node, b: i64 }
var pool: Pair[>..] = [];
fn main() {
    pool.push(Pair { a: Node { next: self, v: 10 }, b: 1 });
    let c = pool.len > 0;
    pool.resize(3, if c { Pair { a: Node { next: pool[0].a, v: 7 }, b: 2 } }
                   else { { Pair { a: Node { next: self, v: 8 }, b: 3 } } });
    print(pool[2].a.next.v);
}
