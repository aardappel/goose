// error: match pattern LIMIT names a local variable, not a constant
// A pattern name resolves as any other name does (§8.1, §11.1): the local
// hides the global constant, and a local is no constant.
let LIMIT = 10;

fn main() {
    let LIMIT = 5;
    var t = 3;
    print(match t { 0..LIMIT => "below", _ => "above" });
}
