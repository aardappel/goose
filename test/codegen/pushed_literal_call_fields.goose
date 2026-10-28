// A struct literal pushed directly, whose fields are call results holding
// relative links, is built where it stays: each link measures from its own
// slot in the pushed element, not from a temporary that is copied afterwards.
struct Node { id: i64, next: Node&<u32>? }
struct Pair { left: Node, right: Node }

fn make(first: Node&, id: i64) -> Node { Node { id: id, next: first } }

fn main() {
    var ps: Pair[>..] = [];
    ps.push(Pair { left: Node { id: 7 }, right: Node { id: 8 } });
    let first .= ps[0].left;
    ps.push(Pair { left: make(first, 1), right: make(first, 2) });
    ps.push(Pair { left: make(first, 3), right: Node { id: 4, next: first } });
    for &pr in ps {
        let l = pr.left.next;
        let r = pr.right.next;
        print(pr.left.id, " ", if l { l.id } else { -1 }, " ", pr.right.id, " ", if r { r.id } else { -1 });
    }
}
