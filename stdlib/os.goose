// The Goose standard library: `os` (docs/stdlib.md): a thin layer over the
// C in src/runtime/runtime_os.h (spec §7.10) -- enough to write tools and
// benchmarks. Text goes into builders the caller passes (§2.2 of the
// design); two-outcome calls return a trailing bool.

import std;

// Files. Paths are UTF-8 everywhere, Windows included.
extern "gs_os_read_file" fn read_file(path: const u8[:], out: u8[>..]&) -> bool;      // appends the whole file
extern "gs_os_write_file" fn write_file(path: const u8[:], data: const u8[:]) -> bool;
// All or nothing: a new file beside `path`, flushed to disk and renamed over it.
extern "gs_os_write_file_atomic" fn write_file_atomic(path: const u8[:], data: const u8[:]) -> bool;
extern "gs_os_append_file" fn append_file(path: const u8[:], data: const u8[:]) -> bool;
extern "gs_os_file_exists" fn file_exists(path: const u8[:]) -> bool;      // a file, not a directory
extern "gs_os_remove_file" fn delete_file(path: const u8[:]) -> bool;
// Replaces `to`, if there is one, in a single step.
extern "gs_os_rename_file" fn rename_file(from: const u8[:], to: const u8[:]) -> bool;

// Directories.
extern "gs_os_make_dir" fn make_dir(path: const u8[:]) -> bool;           // true if one is there afterwards
extern "gs_os_is_dir" fn is_dir(path: const u8[:]) -> bool;
extern "gs_os_remove_dir" fn delete_dir(path: const u8[:]) -> bool;       // an empty one
// Appends each entry's name and a '\n', sorted bytewise, "." and ".." left out.
extern "gs_os_list_dir" fn list_dir(path: const u8[:], out: u8[>..]&) -> bool;

// Standard streams.
extern "gs_os_read_line" fn read_line(out: u8[>..]&) -> bool;    // one line of stdin, no newline; false at end
extern "gs_os_read_stdin" fn read_stdin(out: u8[>..]&);          // everything until end of input
extern "gs_os_write_stdout" fn write_stdout(s: const u8[:]);
extern "gs_os_write_stderr" fn write_stderr(s: const u8[:]);
extern "gs_os_flush_stdout" fn flush_stdout();

// Arguments and environment.
extern "gs_os_arg_count" fn arg_count() -> i64;
extern "gs_os_arg" fn arg(i: i64, out: u8[>..]&);              // appends argument i (0 = the program)
extern "gs_os_getenv" fn env(name: const u8[:], out: u8[>..]&) -> bool;

// Application resources: the entry source's directory in JIT, the executable's
// directory in AOT (Contents/Resources/ in a macOS .app). Absolute UTF-8 with
// a trailing separator, ready to concatenate. Ordinary file paths still use cwd.
// The builder form appends nothing on failure; the string form aborts on failure.
extern "gs_os_resource_dir" fn resource_dir(out: u8[>..]&) -> bool;
fn resource_dir() -> u8[>..] {
    var out: u8[>..] = [];
    if !resource_dir(out) { abort("cannot locate the application resource directory"); }
    return out;
}

// The bytes behind args(): grow-only, so the slices handed out stay valid
// however many times it is called.
var argv_text: u8[>..] = [];

fn args() -> u8[:][>..] {
    var out: u8[:][>..] = [];
    for i in arg_count() {
        let start = argv_text.len;
        arg(i, argv_text);
        out.push(argv_text[start..]);
    }
    return out;
}

// Time.
extern "gs_os_time_ns" fn time_ns() -> i64;      // wall clock, nanoseconds since the epoch
extern "gs_os_clock_ns" fn clock_ns() -> i64;    // monotonic, high resolution
extern "gs_os_sleep_ms" fn sleep_ms(ms: i64);

// A nanosecond count past 2^53 rounds to the nearest f64. A sleep is whole
// milliseconds, truncated: `as!`, since that drops the fraction on purpose.
fn time() -> f64 { time_ns() / 1000000000.0 }
fn clock() -> f64 { clock_ns() / 1000000000.0 }
fn sleep(seconds: f64) { sleep_ms((seconds * 1000.0) as! i64) }

// Entropy, for seeding an rng.
extern "gs_os_random_u64" fn random_seed() -> u64;
