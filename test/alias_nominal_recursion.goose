// Aliases do not make a nominal recursive type structurally recursive.
type Link = Node?;
struct Node { value: i64, next: Link }
type Pair = Node[2];
type OtherLink = Link;

fn main() {
    var tail = Node { 2, null };
    let head = Node { 1, tail };
    let next: OtherLink = head.next;
    if next { print(head.value, " ", next.value); }
    let pair: Pair = [Node { 3, null }, Node { 4, null }];
    print(pair[0].value, " ", pair[1].value);
}
