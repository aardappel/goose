// A git source whose repository or ref starts with '-' is refused before git
// runs, which would read it as an option (--upload-pack runs a command); so is
// a ref starting with '.', which no git ref does and which would name a
// directory outside its place in the cache.
// error: has a repository or ref starting with '-'
// error: has a ref starting with '.'
import x from "--upload-pack=x://y@abcdef1";
import y from "aardappel/goose_tools@--upload-pack=x";
import z from "aardappel/goose_tools@..";
fn main() {}
