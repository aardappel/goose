// error: match pattern WATER names a var global, not a constant
// An integer match's named patterns are constants (§8.1): a var global's
// value is only known at run time.
let GROUND = 0;
var WATER = 1;

fn main() {
    var t = 1;
    print(match t { GROUND => "ground", WATER => "water", _ => "other" });
}
