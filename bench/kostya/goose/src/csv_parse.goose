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

fn generate_data(rows: i64) -> u8[>..] {
    var data: u8[>..] = [];
    for i in rows {
        let c = ('A' + i % 26) as u8;
        let x = next_float(1.0);
        let z = next_float(1.0);
        let y = next_float(1.0);
        data.append("\"point ");
        data.push(c);
        format(data, "\\n, \"\"", i % 100, "\"\"\",");
        format_fixed(data, x, 10);
        data.push(',');
        data.push(',');
        format_fixed(data, z, 10);
        data.push(',');
        let flag = if i % 2 == 0 { "true" } else { "false" };
        format(data, "\"[", flag, "\\n, ", i % 100, "]\",");
        format_fixed(data, y, 10);
        data.push('\n');
    }
    return data;
}

fn parse_points(data: u8[:]) -> Point[>..] {
    var points: Point[>..] = [];
    let ok = csv::each_record(data) { record =>
        let x, xok = parse_flt(record[1]);
        let z, zok = parse_flt(record[3]);
        let y, yok = parse_flt(record[5]);
        assert(xok && zok && yok);
        points.push(Point { x: x, y: y, z: z });
    };
    assert(ok);
    return points;
}

fn run(b: Bench&) -> u32 {
    let rows = config_i64(b.name, "rows");
    let data = generate_data(rows);
    var checksum: u32 = 0;
    measure(b) {
        let points = parse_points(data);
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

        checksum += checksum_f64(x_avg) + checksum_f64(y_avg) + checksum_f64(z_avg);
    };
    return checksum + checksum_str(data);
}
