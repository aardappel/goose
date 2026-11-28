// error: cannot call insert, which shrinks names while line
import std;
import dictionary;

var names = dictionary<u32, u8[..32]> {};

fn main() {
    names.insert(1, "old");
    let value = names.get(1);
    if value {
        let line = trim(value[..]);
        names.insert(1, "new");
        print(line);
    }
}
