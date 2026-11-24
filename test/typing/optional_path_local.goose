struct Node { key: i32, next: Node? }

fn main() {
    let tail = Node { key: 2 };
    let head = Node { key: 1, next: tail };
    var next .= head.next;
    while next && next.key != 0 {
        print(next.key);
        next .= next.next;
    }

    let nodes: Node?[2] = [head, null];
    let first .= nodes[0];
    let second .= nodes[1];
    if first { print(first.key); }
    if second { print(second.key); }
}
