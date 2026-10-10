// Json::Generate: serializes a list of coordinates, each with a name and a
// small map of options, to JSON text on every run.

namespace json_generate;

import std;
import math;
import bench;
import helper;
import json;

// One entry of a coordinate's options map: a string key and an (i32, bool)
// pair, which JSON writes as a two-element array. A dictionary is resizable
// and so can't be part of an element of the coordinates array; the map is
// the list of its entries instead, which serializes to the same object.
struct Opt { key: u8[varint], num: i32, flag: bool }

// A coordinate's name and options are stored inline, so the whole list is
// one block of variable-size records.
struct Coordinate { x: f64, y: f64, z: f64, name: u8[varint], opts: Opt[varint] }

// The resizable coordinates have to be the last field; they are still
// serialized first.
struct JsonData { info: const u8[:], coordinates: Coordinate[>..] }

fn round_to_8(value: f64) -> f64 { round(value * 100000000.0) / 100000000.0 }

fn prepare(n: i64) -> JsonData {
    var data = JsonData { info: "some info", coordinates: [] };
    for i in n {
        let x = round_to_8(next_float(1.0));
        let y = round_to_8(next_float(1.0));
        let z = round_to_8(next_float(1.0));

        var name: u8[>..] = [];
        format_fixed(name, next_float(1.0), 7);
        format(name, " ", next_int(10000));

        data.coordinates.push(Coordinate { x: x, y: y, z: z, name: copy(name),
                                           opts: [Opt { key: "1", num: 1, flag: true }] });
    }
    return data;
}

// What serde's derived Serialize writes, field by field, for each type.
fn to_json(out: u8[>..]&, c: Coordinate&) {
    out.push('{');
    json::format_key(out, "x");
    json::format_number(out, c.x);
    out.push(',');
    json::format_key(out, "y");
    json::format_number(out, c.y);
    out.push(',');
    json::format_key(out, "z");
    json::format_number(out, c.z);
    out.push(',');
    json::format_key(out, "name");
    json::format_string(out, c.name);
    out.push(',');
    json::format_key(out, "opts");
    out.push('{');
    for o, i in c.opts {
        if i > 0 { out.push(','); }
        json::format_key(out, o.key);
        out.push('[');
        json::format_int(out, o.num);
        out.push(',');
        json::format_bool(out, o.flag);
        out.push(']');
    }
    out.push('}');
    out.push('}');
}

// {"coordinates":[...],"info":"some info"}
fn to_json(out: u8[>..]&, data: JsonData&) {
    out.push('{');
    json::format_key(out, "coordinates");
    out.push('[');
    for c, i in data.coordinates {
        if i > 0 { out.push(','); }
        to_json(out, c);
    }
    out.push(']');
    out.push(',');
    json::format_key(out, "info");
    json::format_string(out, data.info);
    out.push('}');
}

// The text the parsing benchmarks prepare: what one run of a generator of
// n coordinates writes.
fn text(n: i64) -> u8[>..] {
    let data = prepare(n);
    var out: u8[>..] = [];
    to_json(out, data);
    return out;
}

fn run(b: Bench&) -> u32 {
    let data = prepare(config_i64(b.name, "coords"));
    var text: u8[>..] = [];
    var result: u32 = 0;
    measure(b) {
        text.clear();
        to_json(text, data);
        if starts_with(text, "{\"coordinates\":") { result++; }
    };
    return result;
}
