// error: global never needs an initializer, since Never has no default value: Never has no variants
// An enum without variants has no value at all, so not the default value a
// var global declared without an initializer starts as (§11.1).
enum Never {}

var never: Never;

fn main() { }
