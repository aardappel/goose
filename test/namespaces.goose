// Namespaces (docs/design/namespaces.md): qualified names, per-namespace
// overload sets, definition-site lookup in generic bodies, shadowed globals,
// qualified types, variants and pools, format hooks and the hash bridge
// across namespaces, same-leaf `return from` targets, and same-leaf
// declarations that stay distinct in the generated C.
import ns.image;
import ns.image_more;
import ns.geometry;
import namespaces_lib;
import dictionary;

// Globals sharing leaf names with namespaced declarations.
struct Pixel { v: i64 }
fn brightness(x: i64) -> i64 { x * 2 }
fn parse(s: u8[:]) -> i64 { image::parse(s) + 1000000 }

// A pool-relative reference field naming the pool by its qualified name: the
// same type as image::Node's own `next`, since both name the one pool.
struct Mark { at: image::Node&<u16 in image::pool>? }

fn main_namespaces() {
    let p = image::Pixel { 10, 20, 30 };
    print(image::brightness(p));             // 60
    print(brightness(3));                    // 6
    print(image::twice_bright(p));           // 120
    print(image::scaled(p, 2));              // 120
    print(p);                                // #10/20/30
    print(str("<", image::invert(p), ">"));  // <#245/235/225>
    let q = Pixel { 7 };
    let g = geometry::Pixel { 1.5, 2.5 };
    print(q.v, " ", geometry::brightness(g), " ", ::brightness(q.v));   // 7 4 14
    // Variants and case functions through qualified names.
    var shapes: image::Shape..[>..] = [];
    shapes.push(image::Shape.Circle { r: 2.0 });
    shapes.push(image::Shape.Square { s: 3.0 });
    shapes.push(image::Shape.Empty);
    for s in shapes {
        print(image::area(s));
        match s {
            Circle c => print(c.r),
            Square sq => print(sq.s),
            Empty => print("empty"),
        }
    }                                        // 12 2 9 3 0 empty
    // A pool named across namespaces.
    image::push_node(1);
    image::push_node(2);
    image::pool[0].next .= image::pool[1];
    var m = Mark { at: null };
    m.at .= image::pool[0];
    let n0 = m.at;
    assert(n0);
    let n1 = n0.next;
    assert(n1);
    print(n0.v, " ", n1.v);                  // 1 2
    // A dictionary keyed by a namespaced type, through the global hash bridge.
    var d = dictionary<image::Key, i64> {};
    d.insert(image::Key { 1, 2 }, 12);
    d.insert(image::Key { 2, 1 }, 21);
    block { let v = d.get(image::Key { 2, 1 }); if v { print(v); } }   // 21
    print(d.contains(image::Key { 1, 1 }));  // false
    // `return ... from parse` lands on image::parse through geometry::parse.
    print(parse("12"));                      // 1001012
    print(parse("1x"));                      // 999999
    // A namespaced thread entry.
    let id = thread_spawn(image::worker, 41);
    print(qget<i64>());                      // 42
    thread_wait(id);
    nslib::main_nslib();                     // 2
}

fn main() { main_namespaces(); }
