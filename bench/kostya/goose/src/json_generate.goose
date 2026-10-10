// Json::Generate: serializes a list of coordinates, each with a name and a
// small map of options, to JSON text on every run.

namespace json_generate;

import std;
import math;
import bench;
import helper;
import json;

// The options map of a coordinate: string keys to an (i32, bool) pair,
// which JSON writes as a two-element array.
struct Opt { key: u8[varint], num: i32, flag: bool }

// A coordinate's name and options are stored inline, so the whole list is
// one block of variable-size records.
struct Coordinate { x: f64, y: f64, z: f64, name: u8[varint], opts: Opt[varint] }

fn round_to_8(v: f64) -> f64 { round(v * 100000000.0) / 100000000.0 }

// format!("{:.7} {}", next_float(1.0), next_int(10000))
fn make_name() -> u8[>..] {
    var s: u8[>..] = [];
    format_fixed(s, next_float(1.0), 7);
    s.push(' ');
    format(s, next_int(10000));
    return s;
}

fn prepare(n: i64) -> Coordinate[>..] {
    var coords: Coordinate[>..] = [];
    for i in n {
        let x = round_to_8(next_float(1.0));
        let y = round_to_8(next_float(1.0));
        let z = round_to_8(next_float(1.0));
        coords.push(Coordinate { x: x, y: y, z: z, name: make_name(),
                                 opts: [Opt { key: "1", num: 1, flag: true }] });
    }
    return coords;
}

fn write_coordinate(out: u8[>..]&, c: Coordinate&) {
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
fn write_data(out: u8[>..]&, coords: Coordinate[:], info: u8[:]) {
    out.push('{');
    json::format_key(out, "coordinates");
    out.push('[');
    for c, i in coords {
        if i > 0 { out.push(','); }
        write_coordinate(out, c);
    }
    out.push(']');
    out.push(',');
    json::format_key(out, "info");
    json::format_string(out, info);
    out.push('}');
}

// The text the parsing benchmarks read: n coordinates, generated and
// serialized once.
fn text(n: i64) -> u8[>..] {
    let coords = prepare(n);
    var out: u8[>..] = [];
    write_data(out, coords, "some info");
    return out;
}

fn run(b: Bench&) -> u32 {
    let coords = prepare(config_i64(b.name, "coords"));
    var text: u8[>..] = [];
    var result: u32 = 0;
    measure(b) {
        text.clear();
        write_data(text, coords, "some info");
        if starts_with(text, "{\"coordinates\":") { result++; }
    };
    return result;
}
