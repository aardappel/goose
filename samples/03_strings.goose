// Strings. There is no string type: a string is any u8 array, a string
// literal is static bytes, a builder is a growable u8 array, and a "view"
// into text is a u8 slice that the checker keeps from dangling. This
// program does the usual things -- build, search, split, parse, format --
// and never copies a byte it does not have to.
//
// Build and run (from this directory):
//   goose -o strings.c 03_strings.goose && cl strings.c && strings

import std;

// A struct with an inline small string: no pointer, no allocation, and a
// `format` overload so print/str/format render it the way it wants.
struct Person { name: u8[..16], age: i32 }

fn format(out: u8[>..]&, p: Person) {
    format(out, p.name, " (", p.age, ")");
}

fn main() {
    // --- building ----------------------------------------------------------
    // A builder is a grow-only byte array on the data stack: appending is a
    // pointer bump, nothing is reallocated, and it goes away with the scope.
    var s: u8[>..] = [];                 // the builder: append, push, format
    s.append("Hello");
    s.push(',');
    s.push(' ');
    format(s, "world! ", 6 * 7, " ", 2.5);
    print(s, " [", s.len, " bytes]");
    let joined = str("pi=", 3.14159, " e=", 2.71828);   // a fresh string, built straight into joined
    print(joined);
    // Padded and based numbers, fixed decimals.
    var t: u8[>..] = [];
    format_int(t, 255, 16, 4, '0'); t.push(' ');
    format_int(t, 255, 2, 10, ' '); t.push(' ');
    format_flt(t, 2.0 / 3.0, 3);
    print(t);

    // --- views into text ---------------------------------------------------
    // A slice is a reference plus a length: `line[a..b]` is the text between
    // a and b, no copy. trim/split/find return slices into the input.
    let line = "  key = some value  ";
    let body = trim(line);
    let eq = find(body, "=");
    let key = trim(body[..eq]);
    let value = trim(body[eq + 1..]);
    print("'", body, "' -> '", key, "' / '", value, "'");
    print(starts_with(body, "key"), " ", ends_with(body, "value"), " ", find(body, "zzz"));

    // --- splitting -----------------------------------------------------------
    // split returns an array of slices into its input: a few bytes per part
    // and no text copied, where most languages hand back a list of strings.
    let csv = "alice,30,paris,,bob,25";
    let parts = split(csv, ',');         // an array of slices, all into csv
    print(parts.len, " parts: ", parts);
    var upper: u8[>..] = [];
    each_split("one two  three", ' ') {  // no array built: a block per part
        if it.len > 0 {
            upper.append(it);
            to_upper(upper[^it.len..]);  // in place, on the copy just appended
            upper.push('|');
        }
    };
    print(upper);
    var out: u8[>..] = [];
    join(out, parts, " | ");
    print(out);

    // --- parsing -------------------------------------------------------------
    for p in parts {
        let v, ok = parse_int(p);
        if ok { print("  number ", v); } else { print("  text '", p, "'"); }
    }
    let f, fok = parse_flt("-2.5e3");
    print(f, " ", fok);
    let h, hok = parse_int("ff", 16);
    print(h, " ", hok);

    // --- comparing and sorting -------------------------------------------
    // An array of variable-size strings cannot be sorted in place (its
    // elements are not all the same size), but an array of slices into it
    // can, and that sorts the strings without moving a byte of text.
    var names: u8[][>..] = [];
    names.push("delta"); names.push("alpha"); names.push("charlie"); names.push("bravo");
    var views: u8[:][>..] = [];
    for n in names { views.push(n[..]); }
    sort(views) { a, b => compare(a, b) < 0 };
    print(views);
    print(compare("abc", "abd"), " ", compare("b", "a"), " ", "abc" == "abc");

    // --- replacing, case -------------------------------------------------
    var r: u8[>..] = [];
    format_replaced(r, "the cat sat on the mat", "at", "og");
    print(r);
    var lower: u8[>..] = [];
    lower.append("MiXeD Case");
    to_lower(lower);
    print(lower);

    // --- bytes and code points -------------------------------------------
    // Text is bytes; UTF-8 is a convention the library understands.
    let greek = "αβγ";
    print(greek.len, " bytes, ", greek[0], " first byte");
    var cps = 0;
    each_utf8(greek) { cps++; };
    var back: u8[>..] = [];
    each_utf8(greek) { push_utf8(back, it + 1); };   // the next letter each
    print(cps, " code points; shifted: ", back);
    // A byte is a number: print shows 97, and it takes a push to append one.
    var caps: u8[>..] = [];
    for c in "abc" { caps.push(c - 'a' + 'A'); }
    print("abc"[0], " ", caps, " ", str('a'));

    // --- inline strings in structs -----------------------------------------
    // A Person is one flat value: the name's bytes live in the struct, so an
    // array of them is a single block, sortable and copyable as plain data.
    var people: Person[>..] = [];
    people.push(Person { name: "ada", age: 36 });
    people.push(Person { name: "grace", age: 45 });
    var p2 = Person { name: "", age: 20 };
    format(p2.name, "linus");            // a limited array is a builder too, up to its capacity
    people.push(p2);
    print(people);
    for &p in people { p.age += 1; }
    print(str(people[0]), " ", people[2].name.len, "/", people[2].name.cap);
}
