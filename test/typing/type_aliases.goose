// Aliases do not make a nominal recursive type structurally recursive.
type Link = Node?;
struct Node { value: i64, next: Link }
type Pair = Node[2];
type OtherLink = Link;

fn links() {
    var tail = Node { 2, null };
    let head = Node { 1, tail };
    let next: OtherLink = head.next;
    if next { print(head.value, " ", next.value); }
    let pair: Pair = [Node { 3, null }, Node { 4, null }];
    print(pair[0].value, " ", pair[1].value);
}

// Aliases name a variant type too, through another alias and with const,
// and a value of such an alias passes to a generic variant parameter.
enum Item<T> { Some { value: T }, None }
type Payload = Item<i64>.Some;
type AnotherPayload = Payload;
type ConstPayload = const Item<i64>.Some;

fn value<T>(p: Item<T>.Some) -> T { p.value }

fn payloads() {
    let a: Payload = Item<i64>.Some { value: 7 };
    let b: AnotherPayload = Item<i64>.Some { value: 9 };
    let c: Item<i64>.Some = Item<i64>.Some { value: 11 };
    print(value(a), value(b), value(c));
}

fn main() {
    links();
    payloads();
}
