// Namespace geometry (docs/design/namespaces.md): leaf names shared with
// image:: and with the root file's globals, all distinct declarations, all
// distinct in the generated C.
namespace geometry;

import .image;

struct Pixel { x: f64, y: f64 }

fn brightness(p: Pixel) -> i64 { (p.x + p.y) as i64 }

// A different parse: image::digit's `return ... from parse` passes through
// this frame to image::parse.
fn parse(s: u8[:]) -> i64 {
    var total = 0;
    for c in s { total = total * 10 + image::digit(c); }
    total
}
