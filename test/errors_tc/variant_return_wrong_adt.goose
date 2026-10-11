// error: expected a value of type Maybe<i64>, got Other<i64>.None
enum Maybe<T> { None, Some { value: T } }
enum Other<T> { None }
fn wrong() -> Maybe<i64> {
    let absent = Other<i64>.None {};
    return absent;
}
fn main() { print(wrong()); }
