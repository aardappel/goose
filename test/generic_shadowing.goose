// Generic type parameters resolve lexically before namespace declarations (§11.1).
struct T { value: i64 }
enum U { Empty }
type V = bool;

fn identity<T>(value: T) -> T { value }
struct Box<U> { value: U }
enum Maybe<V> { Some { value: V }, None }

fn nested<T>(value: T) -> T {
    fn inner(arg: T) -> T { arg }
    inner(value)
}

fn qualified<T>(value: T, ordinary: ::T) -> T {
    print(ordinary.value);
    value
}

fn main() {
    print(identity(42));
    print(Box<i64> { 7 }.value);
    let item: Maybe<i64> = Maybe<i64>.Some { 9 };
    match item {
        Some payload => print(payload.value),
        None => print(0),
    }
    print(nested(12));
    print(qualified(13, T { 14 }));
}
