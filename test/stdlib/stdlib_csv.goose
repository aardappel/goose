// runtime-debug: stdlib/csv.goose's records with plain, empty and quoted
// fields, doubled quotes, separators and line breaks inside quotes, LF and
// CRLF records, blank lines, other separators, each kind of malformed
// record, and format_field's quoting read back by each_record.
import std;
import csv;

// A field as the test prints it, its line breaks and tabs made visible.
fn show(out: u8[>..]&, field: u8[:]) {
    var a: u8[>..] = [];
    var b: u8[>..] = [];
    format_replaced(a, field, "\r", "\\r");
    format_replaced(b, a, "\n", "\\n");
    format_replaced(out, b, "\t", "\\t");
}

fn dump(name: u8[:], text: u8[:], sep: u8) {
    var line: u8[>..] = [];
    format(line, name, ":");
    let ok = csv::each_record(text, sep) { fields =>
        line.append(" [");
        for f, i in fields {
            if i > 0 { line.push('|'); }
            show(line, f);
        }
        line.push(']');
    };
    if !ok { line.append(" malformed"); }
    print(line);
}

fn main() {
    dump("plain", "a,b,c\n1,2,3\n", ',');
    dump("no final break", "a,b\nc,d", ',');
    dump("crlf", "a,b\r\nc,d\r\n", ',');
    dump("empty fields", ",,\na,,b\na,\n,a", ',');
    dump("blank lines", "a\n\nb\n\n", ',');
    dump("crlf blank", "\r\n\r\n", ',');
    dump("empty text", "", ',');
    dump("quoted", "\"a,b\",\"c\"\"d\",\"\"\n", ',');
    dump("quoted breaks", "\"line 1\nline 2\",x\n\"one\r\ntwo\",y\r\n", ',');
    dump("quoted crlf", "\"a\"\r\n\"b\",\"c\"\r\n", ',');
    dump("quoted only quotes", "\"\"\"\"\n\"\"\"\"\"\"", ',');
    dump("lone cr kept", "a\rb,c\r", ',');
    dump("semicolons", "a;b,c;\"d;e\"\n", ';');
    dump("tabs", "a\tb\t\"c\td\"\n", '\t');
    dump("spaces kept", " a , b \n", ',');

    // Malformed: what came before is handed over, the bad record is not.
    dump("bare quote", "ok\na\"b,c\n", ',');
    dump("unterminated", "ok\n\"abc\nd", ',');
    dump("after quote", "ok\n\"a\"b,c\n", ',');
    dump("cr after quote", "\"a\"\rb\n", ',');
    dump("space after quote", "\"a\" ,b\n", ',');

    // The block's `return` leaves the function calling each_record.
    fn first_long(text: u8[:]) -> i64 {
        var n = 0;
        csv::each_record(text) { fields =>
            if fields.len > 2 { return n; }
            n++;
        };
        return -1;
    }
    print(first_long("a\nb,c\nd,e,f\ng\n"), " ", first_long("a\n"));

    // format_field, read back: fields of quotes, separators, line breaks
    // and letters, one to five per record, with doubled quotes undone.
    var r = rng { 99 };
    let bytes = "ab,\"\n\r;";
    var text: u8[>..] = [];
    var want: u8[>..] = [];         // every field, back to back
    var ends: i64[>..] = [];        // where each ends in want
    var counts: i64[>..] = [];
    for k in 2000 {
        let n = 1 + r.rand_int(5);
        counts.push(n);
        for i in n {
            var field: u8[>..] = [];
            for j in r.rand_int(6) { field.push(bytes[r.rand_int(bytes.len)]); }
            if i > 0 { text.push(','); }
            csv::format_field(text, field);
            want.append(field);
            ends.push(want.len);
        }
        text.append(if k % 2 == 0 { "\n" } else { "\r\n" });
    }
    var records = 0;
    var at = 0;
    var wrong = 0;
    let ok = csv::each_record(text) { fields =>
        if fields.len != counts[records] { wrong++; }
        for f in fields {
            var got: u8[>..] = [];
            format_replaced(got, f, "\"\"", "\"");
            let from = if at > 0 { ends[at - 1] } else { 0 };
            if at >= ends.len || got != want[from..ends[at]] { wrong++; }
            at++;
        }
        records++;
    };
    print(ok, " ", records, " records, ", at, " fields, ", wrong, " wrong");

    var fields: u8[>..] = [];
    for s in ["plain", "", "a,b", "say \"hi\"", "two\nlines", "cr\r", "semi;colon"] {
        fields.push('<');
        csv::format_field(fields, s);
        fields.append("> ");
    }
    fields.push('<');
    csv::format_field(fields, "semi;colon", ';');
    fields.push('>');
    var line: u8[>..] = [];
    show(line, fields);
    print(line);
}
