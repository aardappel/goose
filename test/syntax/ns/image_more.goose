// A second file of namespace image (docs/design/namespaces.md): its
// declarations join image::, and the other file's are reached unqualified.
namespace image;

import .image;

fn invert(p: Pixel) -> Pixel {
    Pixel { 255 - p.r, 255 - p.g, 255 - p.b }
}

// A declaration placed in the global namespace from a namespaced file. The
// whole declaration is then written as global code: the type carries its
// qualifier, and the bare `brightness` call is the root file's global one,
// while the UFCS call resolves in the namespace of the receiver's type.
fn ::dimmed(p: image::Pixel) -> i64 { brightness(p.brightness()) / 4 }
