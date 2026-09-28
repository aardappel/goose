// A var global declared with only a type starts as its type's default
// value (§11.1), taken in its place among the globals' initializers:
// integers, a struct with declared defaults (one of which runs a function
// there), a fixed array, an enum, a null optional and a limited array; and
// the types default<T>() does not take, whose default value is empty or
// built field by field: a string, an array of strings, grow-only and
// grow-shrink arrays, a reusable pool, a limited array of construction-time
// capacity (none), a struct holding a string and a varint, one ending in
// a grow-only array, and a variable-mode ADT. A later initializer reads
// some, and each is then assigned, grown or allocated from as its type
// allows: a string, the struct holding one and the ADT are frozen at
// construction (§4.4), and stay as they started.
var ticks: i64 = 0;
fn tick() -> i64 { ticks++; ticks }

struct V { x: f32, y: f32 = 1.5 }
struct Stats { hp: i32 = 100, level: i64, best: i64 = tick() }
enum Shape { Dot, Circle { r: f64 } }
struct Named { name: u8[], n: i64, q: varint }
struct Queue { head: i64, items: i64[>..] }
enum Part { Wheels { sizes: f64[], count: i64 }, Nothing }

var counter: i64;
var small: u8;
var origin: V;
var grid: i64[3];
var shape: Shape;
var stats: Stats;
var marker: V&?;
var recent: i64[..4];
var text: u8[];
var lines: u8[][>..];
var log: i64[>..];
var stack: i64[>..<];
reusable var pool: V[>..];
var sized: i64[..];
var named: Named;
var queue: Queue;
var part: Part..;
let after = counter + stats.best + log.len + lines.len + 10;

fn main() {
    print(counter, " ", small, " ", origin.x, "/", origin.y, " ", grid, " ", stats.hp, " ",
          stats.level, " ", stats.best, " ", ticks, " ", after, " ", recent.len);
    match shape { Dot => print("dot"), Circle c => print(c.r) }
    print(if marker { "marked" } else { "unmarked" });
    print("'", text, "' ", lines.len, " ", log, " ", stack, " ", pool.len, " ", sized.cap, " '",
          named.name, "' ", named.n, " ", named.q, " ", queue.head, " ", queue.items);
    match part { Wheels w => print("wheels ", w.sizes.len, " ", w.count), Nothing => print("nothing") }

    counter += 5;
    small--;
    origin = V { x: 4.0, .. };
    grid[1] = 9;
    shape = Shape.Circle { r: 2.5 };
    stats.level = 3;
    marker .= origin;
    recent.push(7);
    recent.push(8);
    print(counter, " ", small, " ", origin.x, "/", origin.y, " ", grid, " ", stats.level, " ",
          recent);
    match shape { Dot => print("dot"), Circle c => print(c.r) }
    print(if marker { "marked" } else { "unmarked" });

    lines.push("one");
    lines.push("two");
    log.push(1);
    stack.push(2);
    stack.push(3);
    let top = stack.pop();
    let i = pool.alloc_index(V { x: 6.0, .. });
    pool.free(i);
    let j = pool.alloc_index(V { x: 7.0, .. });
    named.n = 1;
    queue = Queue { head: 5, .. };
    queue.items.push(4);
    print(lines, " ", log, " ", stack, " ", top, " ", i, j, " ", pool[0].x, " ", named.n, " ",
          queue.head, " ", queue.items);
}
