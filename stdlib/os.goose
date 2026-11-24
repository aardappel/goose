// The Goose standard library: `os` (docs/stdlib.md): a thin layer over the
// C in src/runtime/runtime_os.h (spec §7.10) -- enough to write tools and
// benchmarks. Text goes into builders the caller passes (§2.2 of the
// design); two-outcome calls return a trailing bool.

import std;

// Return unused complete pages of this thread program's data stacks to the
// OS. Live values, limited-array capacity and virtual reservations remain;
// future growth still works. Call after a large workload, not for every
// temporary. False means an OS request failed.
extern fn trim_memory() -> bool = "gs_trim_memory";

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
extern fn read_stdin(out: u8[>..]&, max: i64) -> bool = "gs_os_read_stdin_max";  // up to max bytes; false at end
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

// Append YYYY-MM-DDTHH:MM:SSZ for a whole Unix second in years 0001-9999.
// Leave out unchanged when the timestamp is outside that range.
fn format_time_utc(out: u8[>..]&, seconds: i64) -> bool {
    if seconds < -62135596800 || seconds > 253402300799 { return false; }
    let day = div_euclid(seconds, 86400);
    let second = seconds % 86400;
    // March-based Gregorian calendar in 400-year eras (civil_from_days).
    // https://howardhinnant.github.io/date_algorithms.html#civil_from_days
    let shifted = day + 719468;
    let era = shifted / 146097;
    let day_of_era = shifted % 146097;
    let year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    let day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    let march_month = (5 * day_of_year + 2) / 153;
    let day_of_month = day_of_year - (153 * march_month + 2) / 5 + 1;
    let month = if march_month < 10 { march_month + 3 } else { march_month - 9 };
    let year = era * 400 + year_of_era + (if month <= 2 { 1 } else { 0 });

    format_int(out, year, 10, 4, '0');
    out.push('-');
    format_int(out, month, 10, 2, '0');
    out.push('-');
    format_int(out, day_of_month, 10, 2, '0');
    out.push('T');
    format_int(out, second / 3600, 10, 2, '0');
    out.push(':');
    format_int(out, second / 60 % 60, 10, 2, '0');
    out.push(':');
    format_int(out, second % 60, 10, 2, '0');
    out.push('Z');
    return true;
}

// Entropy, for seeding an rng.
extern fn random_seed() -> u64 = "gs_os_random_u64";

// Fills out with cryptographically secure random bytes from the OS, for
// keys and nonces: getentropy(3), or ProcessPrng on Windows. False if the OS
// gives none, and then out is not to be used.
extern fn random_bytes(out: u8[:]) -> bool = "gs_os_random_bytes";
