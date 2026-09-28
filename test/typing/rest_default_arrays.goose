// A literal ending in `..` fills an array field it does not give with `[]`
// (§4.2): a string, one counted by a varint, an array with a u16 length,
// an array of strings, limited arrays of static and of construction-time
// capacity (the latter then with none), and a resizable last field,
// grow-only or grow-shrink; a fixed array still gets its elements'
// defaults. `Named { .. }`, a struct of a string and a number, builds both
// from nothing. A field of any other type that is not fixed-size is filled
// the same way, field by field: a nested struct, a resizable struct as the
// last field, a varint (0), a variable-mode ADT (its first variant). A
// variant literal's `..` fills its payload alike, and a generic struct an
// array type argument. Declared `= []` defaults do the same for each of
// those array kinds without `..`. The arrays are then used: pushed to,
// popped, iterated, and literals holding them pushed into an array.
struct Named { name: u8[], n: i64 }
struct Arrays {
    n: i64,
    name: u8[],
    short: u8[varint],
    counted: i64[u16],
    words: u8[][],
    few: i64[..4],
    some: i64[..],
    fixed: i32[2],
    log: i64[>..],
}
struct Stack { n: i64, items: i64[>..<] }
struct Declared {
    name: u8[] = [],
    short: u8[varint] = [],
    words: u8[][] = [],
    few: i64[..4] = [],
    some: i64[..] = [],
    log: i64[>..] = [],
}
struct Inner { tag: u8[], q: varint, k: i64 = 7 }
enum Part { Wheels { sizes: f64[], count: i64 }, Nothing }
struct Outer { n: i64, inner: Inner, part: Part.. }
struct Wrapped { n: i64, tail: Stack }
struct Box<T> { n: i64, v: T }

fn main() {
    let named = Named { .. };
    print("'", named.name, "' ", named.n);                                // '' 0

    var a = Arrays { n: 1, .. };
    print(a.name.len, a.short.len, a.counted.len, a.words.len, a.few.len, " ", a.few.cap, " ",
          a.some.cap, " ", a.fixed, " ", a.log.len);                     // 00000 4 0 [0, 0] 0
    a.few.push(3);
    a.log.push(4);
    a.log.push(5);
    print(a.few, " ", a.log);                                             // [3] [4, 5]

    var s = Stack { .. };
    s.items.push(6);
    s.items.push(7);
    let top = s.items.pop();
    print(s.n, " ", top, " ", s.items);                                   // 0 7 [6]

    var d = Declared {};
    d.few.push(1);
    d.log.push(8);
    print(d.name.len, d.short.len, d.words.len, d.some.cap, " ", d.few, " ", d.log);
                                                                          // 0000 [1] [8]

    let o = Outer { n: 2, .. };
    print(o.n, " '", o.inner.tag, "' ", o.inner.q, " ", o.inner.k);        // 2 '' 0 7
    match o.part {
        Wheels w => print("wheels ", w.sizes.len, " ", w.count),
        Nothing => print("nothing"),
    }                                                                     // wheels 0 0
    let p: Part.. = Part.Wheels { count: 4, .. };
    match p { Wheels w => print(w.sizes.len, " ", w.count), Nothing => print("nothing") }
                                                                          // 0 4
    var w = Wrapped { n: 3, .. };
    w.tail.items.push(10);
    print(w.n, " ", w.tail.n, " ", w.tail.items);                         // 3 0 [10]

    let b = Box<u8[]> { n: 3, .. };
    var c = Box<i64[>..]> { .. };
    c.v.push(9);
    print(b.n, " '", b.v, "' ", c.n, " ", c.v);                            // 3 '' 0 [9]

    var all: Named[>..] = [];
    all.push(Named { n: 5, .. });
    all.push(Named { name: "six", .. });
    all.push(Named { .. });
    for x in all { print("'", x.name, "' ", x.n); }                       // '' 5, 'six' 0, '' 0
}
