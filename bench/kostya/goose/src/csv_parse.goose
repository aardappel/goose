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

fn run(b: Bench&) -> u32 {
    let rows = config_i64(b.name, "rows");
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
        data.append(",,");
        format_fixed(data, z, 10);
        format(data, ",\"[", if i % 2 == 0 { "true" } else { "false" }, "\\n, ", i % 100, "]\",");
        format_fixed(data, y, 10);
        data.push('\n');
    }

    var checksum: u32 = 0;
    measure(b) {
        var points: Point[>..] = [];
        let ok = csv::each_record(data) {
            let x, xok = parse_flt(it[1]);
            let z, zok = parse_flt(it[3]);
            let y, yok = parse_flt(it[5]);
            assert(xok && zok && yok);
            points.push(Point { x: x, y: y, z: z });
        };
        assert(ok);
        guard points.len > 0;
        var x_sum = 0.0;
        var y_sum = 0.0;
        var z_sum = 0.0;
        for p in points {
            x_sum += p.x;
            y_sum += p.y;
            z_sum += p.z;
        }
        let count: f64 = points.len;
        checksum += checksum_f64(x_sum / count) + checksum_f64(y_sum / count) + checksum_f64(z_sum / count);
    };
    return checksum + checksum_str(data);
}
