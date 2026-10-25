// lsp: fixture for test/lsp_protocol.py: completion. The `lsp:` lines at
// the end ask for completions at places and name labels the answer must
// have or lack, or the spelling an item inserts; lines and columns are 1-based.
import std;
import helper;

// A point.
struct Point {
    // Its x.
    x: i64,
    y: i64,
}

enum Shape {
    Circle { r: i64 },
    Square,
}

// The point's x twice.
fn twice_x(p: Point&) -> i64 { p.x * 2 }

fn first<T>(items: T[:], fallback: T) -> T {
    for item, index in items {
        if index == 0 { return item; }
    }
    fallback
}

fn main() {
    let p = Point { x: 1, y: 2 };
    var total = 0;
    for v in [1, 2, 3] {
        total += v;
    }
    let s = Shape.Square;
    let h = helper::twice(total);
    print(p.x + total + twice_x(p) + first([4, 5], 6) + h);
    let q = p.
}

// lsp: error 39
// lsp: complete 38:15 has x
// lsp: complete 38:15 has y
// lsp: complete 38:15 has twice_x
// lsp: complete 38:15 lacks total
// lsp: complete 38:15 lacks len
// lsp: complete 33:9 has total
// lsp: complete 33:9 has v
// lsp: complete 33:9 has p
// lsp: complete 33:9 has Point
// lsp: complete 33:9 has print
// lsp: complete 33:9 has let
// lsp: complete 33:9 has helper::twice
// lsp: complete 33:9 lacks s
// lsp: complete 33:9 lacks q
// lsp: complete 33:9 lacks item
// lsp: complete 24:32 has item
// lsp: complete 24:32 has index
// lsp: complete 24:32 has items
// lsp: complete 24:32 has fallback
// lsp: complete 24:32 has T
// lsp: complete 24:32 lacks total
// lsp: complete 23:24 has items
// lsp: complete 23:24 lacks item
// lsp: complete 35:19 has Square
// lsp: complete 35:19 has Circle
// lsp: complete 35:19 lacks x
// lsp: complete 37:14 has x
// lsp: complete 36:21 has twice
// lsp: complete 36:21 lacks total
// lsp: complete 36:21 inserts twice
// lsp: complete 33:9 inserts helper::twice
// lsp: complete 33:9 inserts total
// lsp: complete 33:9 inserts Point
// lsp: complete 33:9 inserts helper::Box
// lsp: complete 33:9 inserts helper::Choice
// lsp: complete 33:9 inserts helper::Number
// lsp: complete 33:9 inserts helper::answer
// lsp: complete 33:9 inserts helper::counter
// lsp: complete 36:21 inserts Box
// lsp: complete 36:21 inserts Choice
// lsp: complete 36:21 inserts Number
// lsp: complete 36:21 inserts answer
// lsp: complete 36:21 inserts counter
// lsp: complete 36:23 inserts twice
