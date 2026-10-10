// Runs `goose --check` on a saved file and reads the compiler's messages.
// The results point into the output buffer. The caller must keep it alive.
import std;
import os;
import.scan;
import.config;

struct Finding {
    path: const u8[:], line: i64, column: i64, warning: bool, message: const u8[:],
    first_frame: i64, frames: i64,
}
// "in fn f instantiated from file:line", the call chain behind a finding.
struct Frame {
    path: const u8[:], line: i64, name: const u8[:]
}

// Exit status of the run, -1 if the compiler could not be started, or -2 if it
// timed out. With no entry file, a file without fn main() is accepted as a
// module.
fn run_check(file: const u8[:], settings: CheckSettings, compiler: const u8[:], stdlib: const u8[:],
entry: const u8[:], output: u8[>..]&) -> i64 {
    var name: u8[>..] =[];
    if compiler.len > 0 { name.append(compiler); }
    else if !env("GOOSE_COMPILER", name) || name.len == 0 { name.append("goose"); }
    var argv:(const u8[:])[>..] =[];
    argv.push(name);
    if stdlib.len > 0 {
        argv.push("--stdlib");
        argv.push(stdlib);
    }
    argv.push("--check");
    if entry.len == 0 { argv.push("--module"); }
    argv.push(if entry.len > 0 { entry } else { file });
    return run(argv, settings.timeout_ms, output);
}

// The position of the ':' before a number that ends s, and the number, or -1.
fn trailing_number(s: const u8[:]) -> i64, i64 {
    var i = s.len;
    while i > 0 && source_digit(s[i - 1]) { i--; }
    if i == s.len || i == 0 || s[i - 1] != ':' { return -1, 0; }
    let value, ok = parse_int(s[i..]);
    if !ok { return -1, 0; }
    return i - 1, value;
}

fn caret_column(line: const u8[:]) -> i64 {
    var i = 0;
    while i < line.len &&(line[i] == ' ' || line[i] == '\t') { i++; }
    if i >= line.len || line[i] != '^' { return -1; }
    var j = i + 1;
    while j < line.len && line[j] == '~' { j++; }
    while j < line.len && source_space(line[j]) { j++; }
    return if j == line.len { i } else { -1 };
}

fn without_cr(line: const u8[:]) -> const u8[:] {
    return if line.len > 0 && line[line.len - 1] == '\r' { line[..line.len - 1] } else { line };
}

// Reads headers like "path:line[:column]: error|warning: message", the caret
// line after them, and "in ... instantiated from path:line" frames. Other
// lines (progress lines, a bare failure message) are ignored.
fn parse_compiler_output(output: const u8[:], findings: Finding[>..]&, frames: Frame[>..]&) {
    let lines = split(output, '\n');
    for n in lines.len {
        let line = without_cr(lines[n]);
        var kind = 0;
        var at = find(line, ": error: ");
        if at >= 0 { kind = 1; }
        let warning_at = find(line, ": warning: ");
        if warning_at >= 0 &&(at < 0 || warning_at < at) {
            at = warning_at;
            kind = 2;
        }
        if kind != 0 {
            let prefix = line[..at];
            let p1, n1 = trailing_number(prefix);
            if p1 > 0 {
                var path = prefix[..p1];
                var row = n1;
                var column = -1;
                let p2, n2 = trailing_number(prefix[..p1]);
                if p2 > 0 {
                    path = prefix[..p2];
                    row = n2;
                    column = n1 - 1;
                }
                if n + 2 < lines.len {
                    let caret = caret_column(without_cr(lines[n + 2]));
                    if caret >= 0 { column = caret; }
                }
                findings.push(Finding {
                    path: path, line: row - 1, column: column, warning: kind == 2,
                    message: line[at +(if kind == 1 { 9 } else { 11 })..], first_frame: frames.len, frames: 0
                });
            }
            continue;
        }
        let text = trim_start(line);
        if findings.len > 0 && starts_with(text, "in ") {
            let marker = find(text, " instantiated from ");
            if marker < 0 { continue; }
            let origin = text[marker + 19..];
            let p, row = trailing_number(origin);
            if p <= 0 { continue; }
            frames.push(Frame { path: origin[..p], line: row - 1, name: text[3..marker] });
            findings[findings.len - 1].frames++;
        }
    }
}

fn is_status_line(line: const u8[:]) -> bool {
    for prefix in["parsed ok:", "roundtrip ok:", "typechecked ok:", "bce:", "bce-line:", "bce-test:"] {
        if starts_with(line, prefix) { return true; }
    }
    return starts_with(line, "wrote ") && ends_with(line, " bytes)");
}

// The failure text of a run with no located error: the compiler's output
// without its progress lines.
fn failure_message(output: const u8[:], out: u8[>..]&) {
    let lines = split(output, '\n');
    for n in lines.len {
        let line = trim_end(without_cr(lines[n]));
        if line.len == 0 || is_status_line(line) { continue; }
        if out.len > 0 { out.push('\n'); }
        out.append(line);
    }
}
