struct Box<T> { value: T }
type A = Box<A>;
fn main() { var value: A; }
