// Error handling. Goose has no exceptions and no Result type; it has three
// idioms, and this program uses each where it fits: a trailing bool (or
// code) for a two-outcome call, an optional for "found or not", and
// `return ... from` for a failure deep inside nested calls that should land
// in one place. Plus the ones that end the program: assert and abort.
//
// Build and run (from this directory):
//   goose -o errors.c errors.goose && cl errors.c && errors

import std;

struct User { name: u8[..16], age: i32 }

// 1. A trailing bool: the caller writes `let v, ok = ...` and tests ok.
fn parse_age(s: u8[:]) -> i32, bool {
    let v, ok = parse_int(trim(s));
    if !ok || v < 0 || v > 150 { return 0, false; }
    return v as i32, true;
}

// 2. An optional: a reference to the element, or null. The caller must
//    narrow it with if/guard before touching it.
fn find_user(users: User[:], name: u8[:]) -> User? {
    for &u in users { if u.name[..] == name { return u; } }
    return null;
}

// 3. return-from: any failure inside the nested parsing below returns from
//    `load` directly, unwinding parse_record and parse_field with it. The
//    error is the last result, by convention; "" means none.
fn load(text: u8[:]) -> User[>..], u8[] {
    var users: User[>..] = [];
    var lineno = 0;
    each_split(text, '\n') {
        lineno++;
        if it.len > 0 { users.push(parse_record(it, lineno)); }
    };
    return users, "";
}

fn parse_record(line: u8[:], lineno: i64) -> User {
    let comma = find(line, ",");
    guard comma >= 0 else { return [], str("line ", lineno, ": expected name,age") from load; }
    let name = parse_field(line[..comma], lineno);
    let age, ok = parse_age(line[comma + 1..]);
    guard ok else { return [], str("line ", lineno, ": bad age '", trim(line[comma + 1..]), "'") from load; }
    return User { name: name, age: age };
}

fn parse_field(s: u8[:], lineno: i64) -> u8[..16] {
    let t = trim(s);
    guard t.len > 0 else { return [], str("line ", lineno, ": empty name") from load; }
    guard t.len <= 16 else { return [], str("line ", lineno, ": name too long") from load; }
    return t;
}

fn report(text: u8[:]) {
    let users, err = load(text);
    if err.len > 0 { print("error: ", err); return; }
    print("loaded ", users.len, " users");
    for u in users { print("  ", u.name, " ", u.age); }
}

fn main() {
    // A two-outcome call, tested inline.
    for s in ["42", " 7 ", "-3", "abc", "200"] {
        let age, ok = parse_age(s);
        if ok { print("'", s, "' -> ", age); } else { print("'", s, "' is not an age"); }
    }

    // An optional, narrowed by `if` and by `guard`.
    var users: User[>..] = [];
    users.push(User { name: "ada", age: 36 });
    users.push(User { name: "linus", age: 20 });
    let found = find_user(users, "linus");
    if found { print("found ", found.name, ", age ", found.age); }
    let missing = find_user(users, "nobody");
    if missing == null { print("nobody is not a user"); }
    block {
        let u = find_user(users, "ada");
        guard u else { print("no ada"); break; }
        u.age += 1;                      // narrowed: a plain reference here
        print("ada is now ", u.age);
    }

    // Deep failures landing in one place.
    report("ada, 36\nlinus, 20\n");
    report("ada, 36\nlinus\n");
    report("ada, 36\n, 20\n");
    report("ada, x\n");
    report("averyveryverylongname, 1\n");

    // The ones that stop the program: assert for invariants, abort for a
    // fatal condition with a message, exit for a deliberate end with a code.
    // None of them fire here.
    assert(users.len == 2);
    if users.len > 100 { abort("too many users"); }
    if users.len == 0 { exit(1); }
    print("done");
}
