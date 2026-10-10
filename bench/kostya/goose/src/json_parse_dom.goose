// Json::ParseDom: parses the generated text into a document tree on every
// run and averages the coordinates read back out of it.

namespace json_parse_dom;

import std;
import bench;
import helper;
import json;
import json_generate;

// coord[key] as a number; the text is ours, so anything else is a bug.
fn number_at(coord: json::Node&, key: u8[:]) -> f64 {
    let v, ok = json::as_f64(json::member(coord, key));
    if !ok { abort(str("no number at ", key)); }
    return v;
}

fn run(b: Bench&) -> u32 {
    let text = json_generate::text(config_i64(b.name, "coords"));
    var result: u32 = 0;
    measure(b) {
        var doc: json::Node[>..] = [];
        var decoded: u8[>..] = [];
        let root, err = json::parse(text, doc, decoded);
        guard root else { abort(err); }
        let coordinates = json::member(root, "coordinates");
        guard coordinates else { abort("no coordinates"); }
        var x = 0.0;
        var y = 0.0;
        var z = 0.0;
        var len = 0;
        json::each(coordinates) { coord =>
            x += number_at(coord, "x");
            y += number_at(coord, "y");
            z += number_at(coord, "z");
            len++;
        };
        result += checksum_f64(x / len) + checksum_f64(y / len) + checksum_f64(z / len);
    };
    return result;
}
