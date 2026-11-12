// lsp: fixture for test/lsp_protocol.py. The `lsp:` lines below it are the
// queries it asks of the server and what they must answer; lines and
// columns are 1-based, as the compiler counts them.
import std;
import helper;

// A point.
struct Point {
    // Its x.
    x: i64,
    y: i64,
}

// Adds two numbers.
fn add<T>(a: T, b: T) -> T { a + b }

enum Shape {
    Circle { r: i64 },
    Square,
}

fn area(s: Shape) -> i64 {
    match s {
        Circle c => c.r * c.r * 3,
        Square => 1,
    }
}

fn main() {
    let p = Point { x: 1, y: 2 };
    let sum = add(p.x, p.y);
    let f = add(1.5, 2.5);
    print(sum);
    print(f);
    print(area(Shape.Square));
    print(helper::twice(sum));
    var v: i64[>..] = [];
    v.push(sum);
    print(v.len);
}

// Called by nothing.
fn spare(x) { x }

// lsp: hover 31:15 "fn add<T>(a: T, b: T) -> T"
// lsp: hover 31:15 "Adds two numbers."
// lsp: hover 31:21 "x: i64"
// lsp: hover 31:21 "Its x."
// lsp: hover 30:13 "struct Point"
// lsp: hover 24:23 "r: i64"
// lsp: hover 15:30 "parameter a: i64"
// lsp: hover 15:30 "a: f64"
// lsp: hover 15:14 "type parameter T"
// lsp: hover 35:22 "Shape.Square"
// lsp: hover 33:11 "let sum: i64"
// lsp: hover 36:19 "fn helper::twice(x: i64) -> i64"
// lsp: hover 38:7 "builtin .push"
// lsp: hover 39:13 "builtin .len: i64"
// lsp: definition 31:15 -> 15:4
// lsp: definition 31:21 -> 10:5
// lsp: definition 30:13 -> 8:8
// lsp: definition 35:22 -> 19:5
// lsp: definition 33:11 -> 31:9
// lsp: definition 15:30 -> 15:11
// lsp: definition 36:19 -> helper.goose
// lsp: definition 5:3 -> helper.goose
// lsp: symbol Point struct
// lsp: symbol add function
// lsp: symbol Shape enum
// lsp: symbol main function
// lsp: signature 31:19 "fn add<T>(a: T, b: T) -> T" 0
// lsp: signature 31:24 "fn add<T>(a: T, b: T) -> T" 1
// lsp: token 15:4 function
// lsp: token 8:8 struct
// lsp: token 31:15 function
// lsp: token 31:21 property
// lsp: token 33:11 variable
// lsp: token 15:30 parameter
// lsp: token 30:21 property
// lsp: token 24:9 enumMember
// lsp: hover 30:21 "x: i64"
// lsp: hover 24:9 "Shape.Circle { r: i64 }"
// lsp: definition 30:21 -> 10:5
// lsp: definition 24:9 -> 18:5
// lsp: definition 24:21 -> 24:16
// lsp: definition 15:14 -> 15:8
// lsp: references 31:15 3
// lsp: references 15:8 4
// lsp: references 10:5 3
// lsp: references 18:5 2
// lsp: references 24:16 3
// lsp: references 22:9 2
// lsp: references 37:9 3
// lsp: references 36:19 2
// lsp: rename 31:15 plus -> 3
// lsp: rename 30:9 q -> 3
// lsp: rename 15:8 U -> 4
// lsp: rename 24:9 Disc -> 2
// lsp: rename 36:19 double -> 2
// lsp: rename 38:7 shove -> error
// lsp: rename 33:5 shout -> error
// lsp: rename 31:5 be -> error
// lsp: hint 30:10 ": Point"
// lsp: hint 31:12 ": i64"
// lsp: hint 32:10 ": f64"
// lsp: hint 31:19 "a:"
// lsp: hint 31:24 "b:"
// lsp: hint 32:17 "a:"
// lsp: hint 35:16 "s:"
// lsp: hint 36:25 "x:"
// lsp: folding 1-3
// lsp: folding 8-12
// lsp: folding 17-20
// lsp: folding 22-27
// lsp: folding 23-26
// lsp: folding 29-40
// lsp: selection 31:21 -> 31:21-31:22
// lsp: selection 31:21 -> 31:19-31:22
// lsp: selection 31:21 -> 31:15-31:28
// lsp: selection 31:21 -> 29:1-40:2
// lsp: lens 15 "2 instantiations"
// lsp: lens 29 "Run"
// lsp: lens 43 "not instantiated"
// lsp: incoming 15:4 -> main@31,32
// lsp: incoming 31:15 -> main@31,32
// lsp: incoming 36:19 -> main@36
// lsp: incoming 43:4 -> -
// lsp: outgoing 29:4 -> add@31,32 area@35 twice@36
// lsp: outgoing 22:4 -> -
// lsp: incoming 33:11 -> none
