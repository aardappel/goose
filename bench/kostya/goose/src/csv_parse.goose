// CSV::Parse: rows of CSV with quoted text fields (holding commas and doubled
// quotes) are generated once, then parsed back into points every run, whose
// coordinates are averaged.

namespace csv_parse;

import std;
import math;
import bench;
import helper;
import csv;

struct Point { x: f64, y: f64, z: f64 }

struct CsvParse { rows: i64, checksum: u32, data: u8[>..] }

fn new_csv_parse() -> CsvParse { CsvParse { rows: config_i64("CSV::Parse", "rows"), checksum: 0, data: [] } }

fn generate_data(s: CsvParse&) {
    s.data.clear();

    for i in s.rows {
        let c = ('A' + i % 26) as u8;
        let x = next_float(1.0);
        let z = next_float(1.0);
        let y = next_float(1.0);
        s.data.append("\"point ");
        s.data.push(c);
        format(s.data, "\\n, \"\"", i % 100, "\"\"\",");
        format_fixed(s.data, x, 10);
        s.data.push(',');
        s.data.push(',');
        format_fixed(s.data, z, 10);
        s.data.push(',');
        let flag = if i % 2 == 0 { "true" } else { "false" };
        format(s.data, "\"[", flag, "\\n, ", i % 100, "]\",");
        format_fixed(s.data, y, 10);
        s.data.push('\n');
    }
}

fn parse_points(s: CsvParse&) -> Point[>..] {
    var points: Point[>..] = [];
    let ok = csv::each_record(s.data) { record =>
        let x, xok = parse_flt(record[1]);
        let z, zok = parse_flt(record[3]);
        let y, yok = parse_flt(record[5]);
        assert(xok && zok && yok);
        points.push(Point { x: x, y: y, z: z });
    };
    assert(ok);
    return points;
}

fn prepare(s: CsvParse&) { s.generate_data(); }

fn run(s: CsvParse&, iteration_id: i64) {
    let points = s.parse_points();
    guard points.len > 0;

    var x_sum = 0.0;
    var y_sum = 0.0;
    var z_sum = 0.0;
    for point in points {
        x_sum += point.x;
        y_sum += point.y;
        z_sum += point.z;
    }

    let count: f64 = points.len;
    let x_avg = x_sum / count;
    let y_avg = y_sum / count;
    let z_avg = z_sum / count;

    s.checksum += checksum_f64(x_avg) + checksum_f64(y_avg) + checksum_f64(z_avg);
}

fn checksum(s: CsvParse&) -> u32 { s.checksum + checksum_str(s.data) }
