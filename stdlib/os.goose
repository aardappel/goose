// The Goose standard library: `os` (docs/stdlib.md): a thin layer over the
// C in src/runtime/runtime_os.h (spec §7.10) -- enough to write tools and
// benchmarks. Text goes into builders the caller passes (§2.2 of the
// design); two-outcome calls return a trailing bool.

import std;

// Files. Paths are UTF-8 everywhere, Windows included.
extern fn read_file(path: const u8[:], out: u8[>..]&) -> bool = "gs_os_read_file";    // appends the whole file
extern fn write_file(path: const u8[:], data: const u8[:]) -> bool = "gs_os_write_file";
// All or nothing: a new file beside `path`, flushed to disk and renamed over it.
extern fn write_file_atomic(path: const u8[:], data: const u8[:]) -> bool = "gs_os_write_file_atomic";
extern fn append_file(path: const u8[:], data: const u8[:]) -> bool = "gs_os_append_file";
extern fn file_exists(path: const u8[:]) -> bool = "gs_os_file_exists";    // a file, not a directory
extern fn delete_file(path: const u8[:]) -> bool = "gs_os_remove_file";
// Replaces `to`, if there is one, in a single step.
extern fn rename_file(from: const u8[:], to: const u8[:]) -> bool = "gs_os_rename_file";

// Directories.
extern fn make_dir(path: const u8[:]) -> bool = "gs_os_make_dir";         // true if one is there afterwards
extern fn is_dir(path: const u8[:]) -> bool = "gs_os_is_dir";
extern fn delete_dir(path: const u8[:]) -> bool = "gs_os_remove_dir";     // an empty one
// Appends each entry's name and a '\n', sorted bytewise, "." and ".." left out.
extern fn list_dir(path: const u8[:], out: u8[>..]&) -> bool = "gs_os_list_dir";

// Standard streams.
extern fn read_line(out: u8[>..]&) -> bool = "gs_os_read_line";  // one line of stdin, no newline; false at end
extern fn read_stdin(out: u8[>..]&) = "gs_os_read_stdin";        // everything until end of input
extern fn write_stdout(s: const u8[:]) = "gs_os_write_stdout";
extern fn write_stderr(s: const u8[:]) = "gs_os_write_stderr";
extern fn flush_stdout() = "gs_os_flush_stdout";

// Arguments and environment.
extern fn arg_count() -> i64 = "gs_os_arg_count";
extern fn arg(i: i64, out: u8[>..]&) = "gs_os_arg";            // appends argument i (0 = the program)
extern fn env(name: const u8[:], out: u8[>..]&) -> bool = "gs_os_getenv";

// Application resources: the entry source's directory in JIT, the executable's
// directory in AOT (Contents/Resources/ in a macOS .app). Absolute UTF-8 with
// a trailing separator, ready to concatenate. Ordinary file paths still use cwd.
// The builder form appends nothing on failure; the string form aborts on failure.
extern fn resource_dir(out: u8[>..]&) -> bool = "gs_os_resource_dir";
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
extern fn time_ns() -> i64 = "gs_os_time_ns";    // wall clock, nanoseconds since the epoch
extern fn clock_ns() -> i64 = "gs_os_clock_ns";  // monotonic, high resolution
extern fn sleep_ms(ms: i64) = "gs_os_sleep_ms";

// A nanosecond count past 2^53 rounds to the nearest f64. A sleep is whole
// milliseconds, truncated: `as!`, since that drops the fraction on purpose.
fn time() -> f64 { time_ns() / 1000000000.0 }
fn clock() -> f64 { clock_ns() / 1000000000.0 }
fn sleep(seconds: f64) { sleep_ms((seconds * 1000.0) as! i64) }

// Entropy, for seeding an rng.
extern fn random_seed() -> u64 = "gs_os_random_u64";
