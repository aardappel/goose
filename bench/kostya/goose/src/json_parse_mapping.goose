// Json::ParseMapping: parses the generated text straight into a list of
// typed coordinates on every run, passing over the fields it has no use for,
// and averages them.

namespace json_parse_mapping;

import std;
import bench;
import helper;
import json;
import json_generate;

struct Coordinate { x: f64, y: f64, z: f64 }

fn read_coordinate(r: json::Reader&) -> Coordinate {
    var c = Coordinate { x: 0.0, y: 0.0, z: 0.0 };
    json::each_member(r) { key =>
        if key == "x" { c.x = json::read_number(r); }
        else if key == "y" { c.y = json::read_number(r); }
        else if key == "z" { c.z = json::read_number(r); }
        else { json::skip(r); }
    };
    return c;
}

// {"coordinates": [...]}, any other members passed over.
fn read_coordinates(text: u8[:]) -> Coordinate[>..] {
    var r = json::reader(text);
    var coords: Coordinate[>..] = [];
    json::each_member(r) { key =>
        if key == "coordinates" { json::each_element(r) { coords.push(read_coordinate(r)); }; }
        else { json::skip(r); }
    };
    if !json::finish(r) { abort(json::message(r)); }
    return coords;
}

fn run(b: Bench&) -> u32 {
    let text = json_generate::text(config_i64(b.name, "coords"));
    var result: u32 = 0;
    measure(b) {
        let coords = read_coordinates(text);
        var x = 0.0;
        var y = 0.0;
        var z = 0.0;
        for c in coords {
            x += c.x;
            y += c.y;
            z += c.z;
        }
        let len = coords.len;
        result += checksum_f64(x / len) + checksum_f64(y / len) + checksum_f64(z / len);
    };
    return result;
}
