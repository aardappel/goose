// The imported side of namespace_aliases.goose, whose `im` is image: here
// `im` is geometry, and `terrain`, the name of a namespace the importer
// uses, is image. Each alias holds in its own file alone. `h` names a
// namespace that declares nothing.
namespace im = geometry;
namespace terrain = image;
namespace h = hollow;
import .geometry;
import .image;
import .hollow;

fn geo_bright(x: f64, y: f64) -> i64 { im::brightness(im::Pixel { x, y }) }
fn img_bright(r: u8) -> i64 { terrain::brightness(terrain::Pixel { r, r, r }) }
