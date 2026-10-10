import std;
import os;
import.formatter;
import.rpc;
import.config;
import.lsp;

fn main() {
    binary_stdio();
    let argv = args();
    if argv.len > 1 && argv[1] == "--lsp" {
        serve_lsp();
        return;
    }
    var write = false;
    var check = false;
    var files:(const u8[:])[>..] =[];
    for i in 1..argv.len {
        if argv[i] == "--write" || argv[i] == "-w" { write = true; }
        else if argv[i] == "--check" { check = true; }
        else if argv[i] == "--help" || argv[i] == "-h" {
            write_stdout("Goose tools (written in Goose)\nUsage: goose-tools --lsp | [--write | --check] [file.goose ...]\nWithout files, formats stdin to stdout. --check exits 1 for changes.\n");
            return;
        } else if starts_with(argv[i], "-") && argv[i] != "-" {
            write_stderr(str("unknown option: ", argv[i], "\n"));
            exit(2);
        } else { files.push(argv[i]); }
    }
    if write && check {
        write_stderr("choose --write or --check\n");
        exit(2);
    }
    if write && files.len == 0 {
        write_stderr("--write requires files\n");
        exit(2);
    }
    if !write && !check && files.len > 1 {
        write_stderr("use --write or --check for multiple files\n");
        exit(2);
    }
    var status = 0;
    let count = max(1, files.len);
    for i in count {
        var source: u8[>..] =[];
        let file: const u8[:] = if files.len == 0 { "-" } else { files[i] };
        if file == "-" {
            if write {
                write_stderr("cannot --write stdin\n");
                exit(2);
            }
            read_stdin(source);
        } else if !read_file(file, source) {
            write_stderr(str("cannot read ", file, "\n"));
            status = 2;
            continue;
        }
        var formatted: u8[>..] =[];
        var options = FormatterOptions {};
        var error: u8[>..] =[];
        if !formatter_config(file, options, error) {
            write_stderr(str(error, "\n"));
            status = 2;
            continue;
        }
        if !format_source(source, formatted, options) {
            write_stderr(str(file, ": incomplete or unsafe to format\n"));
            status = 2;
            continue;
        }
        if check {
            if source != formatted {
                write_stderr(str(file, ": needs formatting\n"));
                if status == 0 { status = 1; }
            }
        } else if write {
            if source != formatted && !write_file_atomic(file, formatted) {
                write_stderr(str("cannot write ", file, "\n"));
                status = 2;
            }
        } else { write_stdout(formatted); }
    }
    if status != 0 { exit(status); }
}
