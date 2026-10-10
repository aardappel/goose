// The harness every benchmark runs under: the configuration file, and the
// warm-up/timed loop of LangArena's Benchmark trait. A benchmark is one
// function taking a Bench: it prepares its data in locals, hands its `run`
// body to `measure` as a block, and returns its checksum. The locals are the
// benchmark's state, so they persist across the warm-up and the timed
// iterations exactly as the fields of the other languages' objects do.

import std;
import os;
import helper;

// --- the configuration --------------------------------------------------------

// run.js / test.js: a JSON array of objects, each naming a benchmark and its
// integer and string parameters. Values are kept as slices into the file.
struct Field { bench: const u8[:], key: const u8[:], num: i64, text: const u8[:] }

var cfg_text: u8[>..];
var cfg_fields: Field[>..];
var cfg_order: (const u8[:])[>..];

fn load_config(path: const u8[:]) {
    if !read_file(path, cfg_text) { abort(str("cannot read config ", path)); }
    let t: const u8[:] = cfg_text;
    var pos = 0;

    fn skip_ws() {
        while pos < t.len && (t[pos] == ' ' || t[pos] == '\n' || t[pos] == '\r' || t[pos] == '\t') { pos++; }
    }
    fn expect(c: u8) {
        skip_ws();
        if pos >= t.len || t[pos] != c { abort(str("config: unexpected input at byte ", pos)); }
        pos++;
    }
    fn string() -> const u8[:] {
        expect('"');
        let start = pos;
        while pos < t.len && t[pos] != '"' {
            if t[pos] == '\\' { abort("config: escapes are not supported"); }
            pos++;
        }
        let s = t[start..pos];
        pos++;
        return s;
    }

    expect('[');
    skip_ws();
    while pos < t.len && t[pos] != ']' {
        expect('{');
        let first = cfg_fields.len;
        var name: const u8[:] = t[0..0];
        skip_ws();
        while pos < t.len && t[pos] != '}' {
            let key = string();
            expect(':');
            skip_ws();
            if t[pos] == '"' {
                let s = string();
                if key == "name" { name = s; }
                cfg_fields.push(Field { bench: t[0..0], key: key, num: 0, text: s });
            } else {
                let start = pos;
                while pos < t.len && (t[pos] == '-' || (t[pos] >= '0' && t[pos] <= '9')) { pos++; }
                let v, ok = parse_int(t[start..pos]);
                if !ok { abort(str("config: bad number at byte ", start)); }
                cfg_fields.push(Field { bench: t[0..0], key: key, num: v, text: t[0..0] });
            }
            skip_ws();
            if pos < t.len && t[pos] == ',' { pos++; skip_ws(); }
        }
        expect('}');
        for i in first..cfg_fields.len { cfg_fields[i].bench = name; }
        cfg_order.push(name);
        skip_ws();
        if pos < t.len && t[pos] == ',' { pos++; skip_ws(); }
    }
}

fn config_field(bench: u8[:], key: u8[:]) -> Field? {
    for &f in cfg_fields {
        if f.bench == bench && f.key == key { return f; }
    }
    return null;
}

fn config_i64(bench: u8[:], key: u8[:]) -> i64 {
    let f = config_field(bench, key);
    guard f else { print("Config not found for ", bench, ", field: ", key); return 0; }
    return f.num;
}

fn config_s(bench: u8[:], key: u8[:]) -> const u8[:] {
    let f = config_field(bench, key);
    guard f else { print("Config not found for ", bench, ", field: ", key); return ""; }
    return f.text;
}

// --- the timed loop -----------------------------------------------------------

struct Bench { name: const u8[:], iterations: i64, warmup: i64, time: f64 }

fn make_bench(name: const u8[:]) -> Bench {
    let iterations = config_i64(name, "iterations");
    let wf = config_field(name, "warmup_iterations");
    var warmup = max((iterations * 0.2) as! i64, 1);
    if wf && wf.num > 0 { warmup = wf.num; }
    return Bench { name: name, iterations: iterations, warmup: warmup, time: 0.0 };
}

// The Benchmark trait's warmup() and run_all(), with the helper reset
// between them that the harness does: F(iteration) is the benchmark's run().
fn measure<F>(b: Bench&) {
    for i in b.warmup { F(i); }
    reset();
    let t0 = clock();
    for i in b.iterations { F(i); }
    b.time = clock() - t0;
}

// For the benchmarks whose warm-up is not their run: W(iteration) warms up.
fn measure_with<W, F>(b: Bench&) {
    for i in b.warmup { W(i); }
    reset();
    let t0 = clock();
    for i in b.iterations { F(i); }
    b.time = clock() - t0;
}

// --- the runner ---------------------------------------------------------------

fn lower(s: u8[:]) -> u8[>..] {
    var r: u8[>..] = [];
    r.append(s);
    to_lower(r);
    return r;
}

// The runner: D(bench) returns the benchmark's checksum, or false for a name
// with no implementation.
fn run_main<D>() {
    print("start: ", (time() * 1000.0) as! i64);
    let args = args();
    let config: const u8[:] = if args.len > 1 { args[1] } else { "../run.js" };
    let single: const u8[:] = if args.len > 2 { args[2] } else { "" };
    load_config(config);
    let filter = lower(single);
    var summary = 0.0;
    var ok = 0;
    var fails = 0;
    for name in cfg_order {
        if filter.len > 0 && find(lower(name), filter) < 0 { continue; }
        var b = make_bench(name);
        reset();
        let sum, found = D(b);
        if !found {
            print("Warning: Benchmark '", name, "' defined in config but not found in code");
            continue;
        }
        let expected = config_i64(name, "checksum") as! u32;
        var line: u8[>..] = [];
        format(line, name, ": ");
        if sum == expected {
            line.append("OK ");
            ok++;
        } else {
            format(line, "ERR[actual=", sum, ", expected=", expected, "] ");
            fails++;
        }
        line.append("in ");
        format_flt(line, b.time, 3);
        line.append("s");
        print(line);
        summary += b.time;
    }
    var line: u8[>..] = [];
    line.append("Summary: ");
    format_flt(line, summary, 4);
    format(line, "s, ", ok + fails, ", ", ok, ", ", fails);
    print(line);
    write_file("/tmp/recompile_marker", "RECOMPILE_MARKER_0");
    if fails > 0 { exit(1); }
}
