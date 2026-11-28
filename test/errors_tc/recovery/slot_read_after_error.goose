// error: unknown function: missing
import std;
import dictionary;

var names = dictionary<u32, u8[..32]> {};

struct Driver { lines: const u8[:][>..] = [] }

fn drive(d: Driver&) {
    let line = trim(d.lines[0]);
    names.insert(1, []);
    print(line);
}

fn main() {
    missing();
    var text: u8[>..] = [];
    text.append("a");
    var d = Driver {};
    d.lines = split(text, '\n');
    drive(d);
}
