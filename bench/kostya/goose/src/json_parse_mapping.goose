// Json::ParseMapping: parses the generated text straight into typed
// coordinates on every run, passing over the fields it has no use for, and
// averages them.

namespace json_parse_mapping;

import std;
import bench;
import helper;
import json;
import json_generate;

struct CoordinateData { x: f64, y: f64, z: f64 }

struct Coordinates { coordinates: CoordinateData[>..] }

struct JsonParseMapping { n: i64, result_val: u32, text: u8[>..] }

fn new_json_parse_mapping() -> JsonParseMapping {
    JsonParseMapping { n: config_i64("Json::ParseMapping", "coords"), result_val: 0, text: [] }
}

// What serde's derived Deserialize reads for each type: the fields it knows,
// with any other members passed over.
fn read_coordinate_data(r: json::Reader&) -> CoordinateData {
    var c = CoordinateData { x: 0.0, y: 0.0, z: 0.0 };
    json::each_member(r) { key =>
        if key == "x" { c.x = json::read_number(r); }
        else if key == "y" { c.y = json::read_number(r); }
        else if key == "z" { c.z = json::read_number(r); }
        else { json::skip(r); }
    };
    return c;
}

fn read_coordinates(r: json::Reader&) -> Coordinates {
    var data = Coordinates { coordinates: [] };
    json::each_member(r) { key =>
        if key == "coordinates" { json::each_element(r) { data.coordinates.push(read_coordinate_data(r)); }; }
        else { json::skip(r); }
    };
    return data;
}

fn calc(s: JsonParseMapping&, text: u8[:]) -> CoordinateData {
    var r = json::reader(text);
    let data = read_coordinates(r);
    if !json::finish(r) { abort(json::message(r)); }
    let len: f64 = data.coordinates.len;

    var x = 0.0;
    var y = 0.0;
    var z = 0.0;

    for coord in data.coordinates {
        x += coord.x;
        y += coord.y;
        z += coord.z;
    }

    return CoordinateData { x: x / len, y: y / len, z: z / len };
}

fn prepare(s: JsonParseMapping&) {
    var generator = json_generate::new_json_generate(s.n);
    json_generate::prepare(generator);
    json_generate::run(generator, 0);
    s.text = str(json_generate::get_result(generator));
}

fn run(s: JsonParseMapping&, iteration_id: i64) {
    let avg = s.calc(s.text);
    s.result_val += checksum_f64(avg.x) + checksum_f64(avg.y) + checksum_f64(avg.z);
}

fn checksum(s: JsonParseMapping&) -> u32 { s.result_val }
