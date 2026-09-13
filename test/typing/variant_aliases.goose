enum Item<T> { Some { value: T }, None }
type Payload = Item<i64>.Some;
type AnotherPayload = Payload;
type ConstPayload = const Item<i64>.Some;

fn value<T>(p: Item<T>.Some) -> T { p.value }

fn main() {
    let a: Payload = Item<i64>.Some { value: 7 };
    let b: AnotherPayload = Item<i64>.Some { value: 9 };
    let c: Item<i64>.Some = Item<i64>.Some { value: 11 };
    print(value(a), value(b), value(c));
}
