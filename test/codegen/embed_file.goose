// embed_file(path): a file's bytes, read when the program is compiled, as a
// view of static data; the path is relative to the file with the call, and
// each distinct file is read once. A let or const global may name the path.
import std;

let TABLE: const u8[:] = embed_file("embed_file.txt");
let NAME = "embed_file.txt";

fn main() {
    print(TABLE.len);
    print(TABLE);
    let again = embed_file(NAME);
    print(again.len == TABLE.len);
}
