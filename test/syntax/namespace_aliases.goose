// Namespace aliases (docs/design/namespaces.md): `namespace im = image;`
// reads `im::x` as `image::x`, in this file alone. An alias stands for the
// namespace wherever a qualified name may be written: an expression, a type,
// a variant, a pool, a match pattern's constant and `return ... from`. The
// imported files spell their own names; none of them sees these aliases, and
// ns/alias_user.goose declares an `im` of its own, for another namespace, and
// a `terrain` alias, while this file names the namespace terrain.
import ns.image;
namespace im = image;
import ns.terrain;
namespace t = terrain;
import ns.alias_user;

// A global sharing a leaf with the aliased namespace's declaration: `im::`
// still names the namespaced one, and the bare name this global.
fn brightness(x: i64) -> i64 { x * 2 }

// A pool named through the alias: the same type as image::Node's `next`.
struct Mark { at: im::Node&<u16 in im::pool>? }

fn bright(p: im::Pixel) -> i64 { im::brightness(p) }

// Integer constants of another namespace, through an alias, in patterns.
fn classify(k: i64) -> i64 {
    match k {
        t::GATE => 1,
        t::LO..t::HI => 2,
        _ => 0,
    }
}

// A `return ... from` target named through the alias. Nothing calls it from
// inside image::parse, so it is only resolved and checked.
fn bail() -> i64 { return 7 from im::parse; }

fn main() {
    let p = im::Pixel { 1, 2, 3 };
    print(bright(p));                        // 6
    print(brightness(bright(p)));            // 12
    print(p.brightness());                   // 6
    print(p);                                // #1/2/3
    // The alias and the full name are the same type.
    let q: image::Pixel = p;
    print(im::twice_bright(q));              // 12
    let c: im::Shape.Circle = im::Shape.Circle { r: 1.5 };
    print(c.r);                              // 1.5
    print(classify(t::GATE), classify(25), classify(t::FOUR));   // 120
    print(t::kind(9));                       // 2
    im::push_node(1);
    im::push_node(2);
    im::pool[0].next .= image::pool[1];
    var m = Mark { at: null };
    m.at .= im::pool[0];
    let n0 = m.at;
    assert(n0);
    let n1 = n0.next;
    assert(n1);
    print(n0.v, " ", n1.v);                  // 1 2
    print(im::parse("12"));                  // 1012
    // The imported file's aliases: its `im` is geometry, its `terrain` image.
    print(geo_bright(1.5, 2.5), " ", img_bright(2));   // 4 6
    print(terrain::kind(7));                 // 1
}
