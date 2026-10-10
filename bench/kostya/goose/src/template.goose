// Template::Regex and Template::Parse: render an HTML template by replacing
// its {{ NAME }} placeholders with the values of a map, once with a regular
// expression and once with a hand-written scan.

namespace template;

import std;
import dictionary;
import bench;
import helper;
import regex;

let FIRST_NAMES = ["John", "Jane", "Bob", "Alice", "Charlie", "Diana", "Sarah", "Mike"];
let LAST_NAMES = ["Smith", "Johnson", "Brown", "Taylor", "Wilson", "Davis", "Miller", "Jones"];
let CITIES = ["New York", "Los Angeles", "Chicago", "Houston", "Phoenix", "San Francisco"];
let LOREM = "Lorem {ipsum} dolor {sit} amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore {et} dolore magna aliqua. ";

// `prefix` and then i, added to `keys`; the slice of it that holds them.
fn key(keys: u8[>..]&, prefix: u8[:], i: i64) -> const u8[:] {
    let start = keys.len;
    format(keys, prefix, i);
    return keys[start..];
}

// The reference's TemplateBase.prepare: the template and its variables. A
// dictionary's keys are fixed-size, so the variables' names are slices of
// `keys`, which holds their text.
fn prepare(count: i64, text: u8[>..]&, keys: u8[>..]&, vars: dictionary<const u8[:], const u8[:]>&) {
    vars.clear();
    text.append("<html><body>");
    text.append("<h1>{{TITLE}}</h1>");
    vars.insert("TITLE", "Template title");
    text.append("<p>");
    text.append(LOREM);
    text.append("</p>");
    text.append("<table>");
    for i in count {
        if i % 3 == 0 { text.append("<!-- {comment} -->"); }
        text.append("<tr>");
        format(text, "<td>{{ FIRST_NAME", i, " }}</td>");
        format(text, "<td>{{LAST_NAME", i, "}}</td>");
        format(text, "<td>{{  CITY", i, "  }}</td>");
        vars.insert(key(keys, "FIRST_NAME", i), FIRST_NAMES[i % FIRST_NAMES.len]);
        vars.insert(key(keys, "LAST_NAME", i), LAST_NAMES[i % LAST_NAMES.len]);
        vars.insert(key(keys, "CITY", i), CITIES[i % CITIES.len]);
        format(text, "<td>{balance: ", i % 100, "}</td>");
        text.append("</tr>\n");
    }
    text.append("</table>");
    text.append("</body></html>");
}

// --- Template::Regex ------------------------------------------------------------

let TEMPLATE_REGEX = regex::compile("""\{\{(.*?)\}\}""");

fn run_regex(b: Bench&) -> u32 {
    let count = config_i64(b.name, "count");
    var text: u8[>..] = [];
    var keys: u8[>..] = [];
    var vars = dictionary<const u8[:], const u8[:]> {};
    prepare(count, text, keys, vars);
    var rendered: u8[>..] = [];
    var checksum: u32 = 0;
    measure(b) {
        rendered.clear();
        regex::format_replaced(rendered, TEMPLATE_REGEX, text) { caps =>
            let value = vars.get(trim(regex::group(caps, 1)));
            if value { value } else { "" }
        };
        checksum += rendered.len as! u32;
    };
    return checksum + checksum_str(rendered);
}

// --- Template::Parse ------------------------------------------------------------

fn run_parse(b: Bench&) -> u32 {
    let count = config_i64(b.name, "count");
    var text: u8[>..] = [];
    var keys: u8[>..] = [];
    var vars = dictionary<const u8[:], const u8[:]> {};
    prepare(count, text, keys, vars);
    var rendered: u8[>..] = [];
    var checksum: u32 = 0;
    measure(b) {
        rendered.clear();
        let len = text.len;
        var i = 0;
        while i < len {
            if i + 1 < len && text[i] == '{' && text[i + 1] == '{' {
                var j = i + 2;
                while j + 1 < len {
                    if text[j] == '}' && text[j + 1] == '}' { break; }
                    j++;
                }
                if j + 1 < len {
                    let value = vars.get(trim(text[i + 2..j]));
                    if value { rendered.append(value); }
                    i = j + 2;
                    continue;
                }
            }
            rendered.push(text[i]);
            i++;
        }
        checksum += rendered.len as! u32;
    };
    return checksum + checksum_str(rendered);
}
