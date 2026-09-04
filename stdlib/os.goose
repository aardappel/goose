// The Goose standard library: `os` (docs/stdlib.md): a thin layer over the
// C in src/runtime/runtime_os.h (spec §7.10) -- enough to write tools and
// benchmarks. Text goes into builders the caller passes (§2.2 of the
// design); two-outcome calls return a trailing bool.

import std;

// Files.
extern "gs_os_read_file" fn read_file(path: u8[:], out: u8[>..]&) -> bool;      // appends the whole file
extern "gs_os_write_file" fn write_file(path: u8[:], data: u8[:]) -> bool;
extern "gs_os_append_file" fn append_file(path: u8[:], data: u8[:]) -> bool;
extern "gs_os_file_exists" fn file_exists(path: u8[:]) -> bool;
extern "gs_os_remove_file" fn delete_file(path: u8[:]) -> bool;

// Standard streams.
extern "gs_os_read_line" fn read_line(out: u8[>..]&) -> bool;    // one line of stdin, no newline; false at end
extern "gs_os_read_stdin" fn read_stdin(out: u8[>..]&);          // everything until end of input
extern "gs_os_write_stdout" fn write_stdout(s: u8[:]);
extern "gs_os_write_stderr" fn write_stderr(s: u8[:]);
extern "gs_os_flush_stdout" fn flush_stdout();

// Arguments and environment.
extern "gs_os_arg_count" fn arg_count() -> i64;
extern "gs_os_arg" fn arg(i: i64, out: u8[>..]&);              // appends argument i (0 = the program)
extern "gs_os_getenv" fn env(name: u8[:], out: u8[>..]&) -> bool;

fn args() -> u8[][>..] {
    var out: u8[][>..] = [];
    for i in arg_count() {
        var a: u8[>..] = [];
        arg(i, a);
        out.push(copy(a));
    }
    return out;
}

// Time.
extern "gs_os_time_ns" fn time_ns() -> i64;      // wall clock, nanoseconds since the epoch
extern "gs_os_clock_ns" fn clock_ns() -> i64;    // monotonic, high resolution
extern "gs_os_sleep_ms" fn sleep_ms(ms: i64);

fn time() -> f64 { (time_ns() as f64) / 1000000000.0 }
fn clock() -> f64 { (clock_ns() as f64) / 1000000000.0 }
fn sleep(seconds: f64) { sleep_ms((seconds * 1000.0) as i64) }

// Entropy, for seeding an rng.
extern "gs_os_random_u64" fn random_seed() -> u64;
