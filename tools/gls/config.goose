import json;
import os;
import.scan;
import.rpc;

struct FormatterOptions {
    indent_width: i64 = 4, insert_spaces: bool = true,
    single_line_expressions: bool = true,
    max_blank_lines_between_functions: i64 = 1,
    max_line_width: i64 = 100, max_compact_statements: i64 = 1,
}

fn config_parent(path: const u8[:]) -> const u8[:] {
    if path.len == 3 && path[1] == ':' &&(path[2] == '/' || path[2] == '\\') { return path; }
    var end = path.len;
    while end > 1 &&(path[end - 1] == '/' || path[end - 1] == '\\') { end--; }
    var slash = end - 1;
    while slash >= 0 && path[slash] != '/' && path[slash] != '\\' { slash--; }
    if slash < 0 { return "."; }
    if slash == 0 { return path[..1]; }
    if slash == 2 && path[1] == ':' { return path[..3]; }
    return path[..slash];
}

// File URIs may spell the same path with `.` or `..` components. Normalize
// them before looking for project settings, so a path cannot pick up a config
// from a directory the file is not in.
fn normalize_config_path(path: const u8[:], out: u8[>..]&) {
    var parts:(const u8[:])[>..] =[];
    var pos = 0;
    var root_length = 0;
    if starts_with(path, "/") {
        out.push('/');
        pos = 1;
        root_length = 1;
    } else if path.len >= 3 && path[1] == ':' &&(path[2] == '/' || path[2] == '\\') {
        out.append(path[..2]);
        out.push('/');
        pos = 3;
        root_length = 3;
    }
    while pos <= path.len {
        let start = pos;
        while pos < path.len && path[pos] != '/' && path[pos] != '\\' { pos++; }
        let part = path[start..pos];
        if part.len == 0 || part == "." {}
        else if part == ".." {
            if parts.len > 0 { parts.pop(); }
        } else { parts.push(part); }
        pos++;
    }
    for part in parts {
        if out.len > root_length && out[out.len - 1] != '/' { out.push('/'); }
        out.append(part);
    }
}

// The closest ancestor wins, and gls.json beats .gls.
// Files are read on every request, so config edits take effect right away.
// Returns 1 with the file's bytes, name (filename[selected..]) and directory,
// 0 if there is none, and -1 on error.
fn find_project_config(path: const u8[:], bytes: u8[>..]&, filename: u8[>..]&, selected: i64&,
directory_out: u8[>..]&, error: u8[>..]&) -> i64 {
    var absolute: u8[>..] =[];
    if !starts_with(path, "/") && !(path.len > 1 && path[1] == ':') {
        if !working_directory(absolute) {
            error.append("cannot read working directory");
            return -1;
        }
        absolute.push('/');
    }
    absolute.append(if path == "-" || path == "" { "stdin.goose" } else { path });
    var normalized: u8[>..] =[];
    normalize_config_path(absolute, normalized);
    var directory: const u8[:] = config_parent(normalized);
    loop {
        selected = filename.len;
        format(filename, directory, "/gls.json");
        if file_exists(filename[selected..]) {
            if !read_file(filename[selected..], bytes) {
                error.append("cannot read project configuration");
                return -1;
            }
            break;
        }
        selected = filename.len;
        format(filename, directory, "/.gls");
        if file_exists(filename[selected..]) {
            if !read_file(filename[selected..], bytes) {
                error.append("cannot read project configuration");
                return -1;
            }
            break;
        }
        if directory == "." || directory == "/" { return 0; }
        let parent = config_parent(directory);
        if parent == directory { return 0; }
        directory = parent;
    }
    directory_out.append(directory);
    return 1;
}

fn formatter_config(path: const u8[:], options: FormatterOptions&, error: u8[>..]&) -> bool {
    var bytes: u8[>..] =[];
    var filename: u8[>..] =[];
    var directory: u8[>..] =[];
    var selected = 0;
    let found = find_project_config(path, bytes, filename, selected, directory, error);
    if found < 0 { return false; }
    if found == 0 { return true; }
    var nodes: json::Node[>..] =[];
    var strings: u8[>..] =[];
    let root, parse_error = json::parse(bytes, nodes, strings);
    if !json_is_object(root) {
        format(error, filename[selected..], ": expected a JSON object");
        return false;
    }
    let settings = json::member(root, "formatter");
    if !settings { return true; }
    if !json_is_object(settings) {
        format(error, filename[selected..], ": formatter must be an object");
        return false;
    }
    var item = settings.first;
    while item {
        let key = item.key;
        if key == "singleLineExpressions" || key == "insertSpaces" {
            let flag, is_flag = json::as_bool(item);
            if !is_flag {
                format(error, filename[selected..], ": ", key, " must be a boolean");
                return false;
            }
            if key == "singleLineExpressions" { options.single_line_expressions = flag; }
            else { options.insert_spaces = flag; }
        } else if key == "maxBlankLinesBetweenFunctions" || key == "indentWidth" || key == "maxLineWidth" || key == "maxCompactStatements" {
            let number = json_integer(item, -1);
            var minimum = 0;
            var limit = 100;
            if key == "indentWidth" {
                minimum = 1;
                limit = 16;
            }
            else if key == "maxLineWidth" {
                minimum = 20;
                limit = 1000;
            }
            else if key == "maxCompactStatements" {
                minimum = 1;
                limit = 20;
            }
            if number < minimum || number > limit {
                format(error, filename[selected..], ": invalid ", key);
                return false;
            }
            if key == "indentWidth" { options.indent_width = number; }
            else if key == "maxLineWidth" { options.max_line_width = number; }
            else if key == "maxCompactStatements" { options.max_compact_statements = number; }
            else { options.max_blank_lines_between_functions = number; }
        } else {
            format(error, filename[selected..], ": unknown formatter option ", key);
            return false;
        }
        item .= item.next;
    }
    return true;
}

fn uri_filename(uri: const u8[:], out: u8[>..]&) {
    if !starts_with(uri, "file://") { return; }
    var pos = 7;
    if source_has(uri, pos, "localhost/") { pos += 9; }
    if source_at(uri, pos) != '/' { return; }
    if pos + 3 < uri.len && uri[pos + 2] == ':' { pos++; }
    while pos < uri.len {
        if uri[pos] == '%' && pos + 2 < uri.len {
            let hi = json::hex_digit(uri[pos + 1]);
            let lo = json::hex_digit(uri[pos + 2]);
            if hi >= 0 && lo >= 0 {
                out.push((hi * 16 + lo) as u8);
                pos += 3;
                continue;
            }
        }
        out.push(uri[pos]);
        pos++;
    }
}

// What the language server needs to run the compiler on a saved file.
// Strings are empty when the file does not set them. A relative `stdlib` or
// `entry` is relative to the directory of the configuration file.
struct CheckSettings {
    enabled: bool = true, timeout_ms: i64 = 15000
}

fn check_config(path: const u8[:], settings: CheckSettings&, compiler: u8[>..]&, stdlib: u8[>..]&,
entry: u8[>..]&, error: u8[>..]&) -> bool {
    var bytes: u8[>..] =[];
    var filename: u8[>..] =[];
    var directory: u8[>..] =[];
    var selected = 0;
    let found = find_project_config(path, bytes, filename, selected, directory, error);
    if found < 0 { return false; }
    if found == 0 { return true; }
    var nodes: json::Node[>..] =[];
    var strings: u8[>..] =[];
    let root, parse_error = json::parse(bytes, nodes, strings);
    if !json_is_object(root) {
        format(error, filename[selected..], ": expected a JSON object");
        return false;
    }
    let section = json::member(root, "check");
    if !section { return true; }
    if !json_is_object(section) {
        format(error, filename[selected..], ": check must be an object");
        return false;
    }
    var item = section.first;
    while item {
        let key = item.key;
        if key == "enabled" {
            let flag, is_flag = json::as_bool(item);
            if !is_flag {
                format(error, filename[selected..], ": ", key, " must be a boolean");
                return false;
            }
            settings.enabled = flag;
        } else if key == "timeoutMs" {
            let number = json_integer(item, -1);
            if number < 1000 || number > 300000 {
                format(error, filename[selected..], ": invalid ", key);
                return false;
            }
            settings.timeout_ms = number;
        } else if key == "compiler" || key == "stdlib" || key == "entry" {
            let value, is_string = json::as_string(item);
            if !is_string {
                format(error, filename[selected..], ": ", key, " must be a string");
                return false;
            }
            var target: u8[>..]& = if key == "compiler" { compiler } else if key == "stdlib" {
                stdlib
            } else {
                entry
            };
            // A bare name is looked up on PATH. Anything else is a path.
            let relative = value.len > 0 && !starts_with(value, "/") && !starts_with(value, "\\") && !(value.len > 1 && value[1] == ':');
            let bare = key == "compiler" && find(value, "/") < 0 && find(value, "\\") < 0;
            if relative && !bare { format(target, directory, "/", value); } else {
                target.append(value);
            }
        } else {
            format(error, filename[selected..], ": unknown check option ", key);
            return false;
        }
        item .= item.next;
    }
    return true;
}
