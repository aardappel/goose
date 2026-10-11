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

// The info is a string of its own rather than a view of the literal: a value
// holding no references is one the text can be rebuilt from in place.
struct JsonData { coordinates: Coordinate[], info: u8[] }

// A struct has at most one growable field, its last: here the text, which
// each run makes afresh. The data never changes once made, so it is a
// variable-size value ahead of the text, which Goose builds only where it is
// constructed: by the constructor, as Crystal's Generate makes it, rather
// than by prepare, as Rust's does.
//
// This is not idiomatic Goose, which would keep the text in a local of the
// run and make the data the growable field: a field behind a variable-size
// one is found by walking it, here every coordinate, each time the run
// reaches the text or the info.
struct JsonGenerate { n: i64, result: u32, data: JsonData, text: u8[>..] }

fn round_to_8(value: f64) -> f64 { round(value * 100000000.0) / 100000000.0 }

fn new_json_generate(n: i64 = config_i64("Json::Generate", "coords")) -> JsonGenerate {
    var data: Coordinate[>..] = [];
    for i in n {
        let x = round_to_8(next_float(1.0));
        let y = round_to_8(next_float(1.0));
        let z = round_to_8(next_float(1.0));

        var name: u8[>..] = [];
        format_fixed(name, next_float(1.0), 7);
        format(name, " ", next_int(10000));

        data.push(Coordinate { x: x, y: y, z: z, name: copy(name),
                               opts: [Opt { key: "1", num: 1, flag: true }] });
    }
    return JsonGenerate {
        n: n,
        result: 0,
        data: JsonData { coordinates: copy(data), info: "some info" },
        text: [],
    };
}

fn get_result(s: JsonGenerate&) -> const u8[:] { s.text }

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

// serde_json::to_string: the text of `data` as a new string.
fn to_string(data: JsonData&) -> u8[>..] {
    var out: u8[>..] = [];
    to_json(out, data);
    return out;
}

fn prepare(s: JsonGenerate&) {}

fn run(s: JsonGenerate&, iteration_id: i64) {
    s.text = to_string(s.data);
    if starts_with(s.text, "{\"coordinates\":") { s.result++; }
}

fn checksum(s: JsonGenerate&) -> u32 { s.result }
