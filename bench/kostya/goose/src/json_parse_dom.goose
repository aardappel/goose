// Json::ParseDom: parses the generated text into a document tree on every
// run and averages the coordinates read back out of it.

namespace json_parse_dom;

import std;
import bench;
import helper;
import json;
import json_generate;

struct JsonParseDom { n: i64, result_val: u32, text: u8[>..] }

fn new_json_parse_dom() -> JsonParseDom {
    JsonParseDom { n: config_i64("Json::ParseDom", "coords"), result_val: 0, text: [] }
}

// coord[key] as a number; the text is ours, so anything else is a bug.
fn number_at(coord: json::Node&, key: u8[:]) -> f64 {
    let v, ok = json::as_f64(json::member(coord, key));
    if !ok { abort(str("no number at ", key)); }
    return v;
}

fn calc(s: JsonParseDom&, text: u8[:]) -> f64, f64, f64 {
    var doc: json::Node[>..] = [];
    var decoded: u8[>..] = [];
    let root, err = json::parse(text, doc, decoded);
    guard root else { abort(err); }
    let coordinates = json::member(root, "coordinates");
    guard coordinates else { abort("no coordinates"); }
    // An array node keeps its element count in its value.
    var len = 0.0;
    match coordinates.val {
        Arr a => { len = a.count; },
        _ => { abort("coordinates is not an array"); },
    }

    var x = 0.0;
    var y = 0.0;
    var z = 0.0;

    json::each(coordinates) { coord =>
        x += number_at(coord, "x");
        y += number_at(coord, "y");
        z += number_at(coord, "z");
    };

    return x / len, y / len, z / len;
}

fn prepare(s: JsonParseDom&) {
    var generator = json_generate::new_json_generate(s.n);
    json_generate::prepare(generator);
    json_generate::run(generator, 0);
    s.text = str(json_generate::get_result(generator));
}

fn run(s: JsonParseDom&, iteration_id: i64) {
    let x, y, z = s.calc(s.text);
    s.result_val += checksum_f64(x) + checksum_f64(y) + checksum_f64(z);
}

fn checksum(s: JsonParseDom&) -> u32 { s.result_val }
