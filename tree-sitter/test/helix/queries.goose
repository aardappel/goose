namespace demo;
import std;

// Queries are checked syntactically, without type checking this fixture.
root struct Store {
    items: i32[],
}

enum Result<T> { Value { item: T }, Empty }
type Count = u32;
const limit = 1_000;

extern "example" {
    fn foreign(value: i32) -> i32;
}

fn demo::inspect<T>(value: T, other: i32) {
    let flag = true;
    let fraction = 1.25;
    let letter = 'a';
    let text = "line\n";
    let empty = Result<T>.Empty;
    let item = Result<T>.Value { item: value };
    const if same<T, i32> {
        foreign(other);
    }
    const for <Name, Field> in fields<T> {
        use_field(value);
    }
    defer { cleanup(); }
    each(items) { element: i32 => element + it }
    value.visit<i32>(other);
    /* Nested /* block */ comment. */
}
