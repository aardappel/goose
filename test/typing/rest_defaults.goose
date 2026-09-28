// A struct or variant literal ending in `..` (§4.2): each field it does not
// give takes its declared default, else its type's default value, as
// default<T>() gives it -- a nested struct with its own declared defaults,
// fixed and limited arrays, an enum's first variant, a null optional. Named
// and positional initializers, none at all, a variant literal, generic
// structs given and inferred, a `..` that leaves nothing out, a literal
// assigned to a global and literals built in place in an array. Declared
// defaults run at each literal, in field order among the given
// initializers.
var ticks: i64 = 0;
fn tick() -> i64 { ticks++; ticks }

struct V { x: f32, y: f32 = 1.5 }
enum Mode { Idle, Walk { speed: f64 } }
struct Entity {
    kind: i64,
    id: i64 = tick(),
    pos: V,
    hp: i32 = 100,
    name: u8[] = "anon",
    slots: i32[3],
    tags: u8[..8],
    mode: Mode,
    target: V&?,
    serial: i64 = tick(),
}
enum Shape { Dot, Circle { r: f64, c: V, tag: i64 = 7 } }
struct Pair<T> { a: T, b: T }

var origin = V { x: 0.5, .. };

fn show(e: Entity&) {
    print(e.kind, " ", e.id, " ", e.pos.x, "/", e.pos.y, " ", e.hp, " ", e.name, " ",
          e.slots, " ", e.tags.len, " ", match e.mode { Idle => "idle", Walk => "walk" },
          " ", if e.target { "target" } else { "none" }, " ", e.serial);
}

fn main() {
    print(origin.x, "/", origin.y);
    origin = V { x: 4.0, .. };
    print(origin.x, "/", origin.y);

    var a = Entity { kind: 3, .. };
    show(a);
    let b = Entity { kind: 4, hp: 7, name: "bee", .. };
    show(b);
    let c = Entity { 5, 6, V { x: 2.0 }, .. };
    show(c);
    let d = Entity { .. };
    show(d);
    let e = Entity { kind: tick() * 10, .. };
    show(e);
    a.slots[1] = 8;
    a.tags.push(1);
    a.target .= origin;
    show(a);
    var es: Entity[>..] = [];
    es.push(Entity { kind: 9, name: "pushed", .. });
    es.push(Entity { .. });
    for x in es { show(x); }

    let s: Shape = Shape.Circle { r: 2.0, .. };
    match s { Circle k => print(k.r, " ", k.c.x, "/", k.c.y, " ", k.tag), Dot => print("dot") }
    let p = Pair<i64> { a: 3, .. };
    let q = Pair { a: 1.5, .. };
    let w = V { x: 1.0, y: 2.0, .. };
    print(p.a, " ", p.b, " ", q.a, " ", q.b, " ", w.x, "/", w.y);
}
