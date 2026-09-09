// A second file of namespace image (docs/design/namespaces.md): its
// declarations join image::, and the other file's are reached unqualified.
namespace image;

import .image;

fn invert(p: Pixel) -> Pixel {
    Pixel { (255 - p.r) as u8, (255 - p.g) as u8, (255 - p.b) as u8 }
}

// A declaration placed in the global namespace from a namespaced file. The
// whole declaration is then written as global code, so the key type carries
// its qualifier. This is the bridge the global dictionary module needs for
// an image::Key: its `hash(key)` resolves where it is defined, globally.
fn ::hash(k: image::Key) -> u64 { image::hash(k) }
