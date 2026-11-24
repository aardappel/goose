// error: only variables can be narrowed: bind `node.next`
// error: only variables can be narrowed: bind `node.next`
// error: only variables can be narrowed: bind `node.next`
// error: only variables can be narrowed: bind `nodes[0]`
// error: only variables can be narrowed: bind `node.next`
// error: optional value must be narrowed (if/guard/assert) before use
struct Node { key: i32, next: Node? }

fn in_if(node: Node&) {
    if node.next { print(node.next.key); }
}

fn in_condition(node: Node&) {
    while node.next && node.next.key != 0 {}
}

fn in_loop(node: Node&) {
    while node.next { print(node.next.key); }
}

fn indexed(nodes: Node?[:]) {
    if nodes[0] { print(nodes[0].key); }
}

fn write_field(node: Node&) {
    if node.next { node.next.key = 3; }
}

fn unchecked(node: Node?) {
    print(node.key);
}

fn main() {
    let node = Node { key: 1 };
    let nodes: Node?[1] = [node];
    in_if(node);
    in_condition(node);
    in_loop(node);
    indexed(nodes);
    write_field(node);
    unchecked(node);
}
