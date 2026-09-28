// error: variable LATE may be used before it is assigned: globals initialize in declaration order, imported files first
// error: global_before_init_import.goose:7 has run
// An imported file's globals initialize first (§11.1): EARLY's initializer
// there calls first(), which reads LATE before LATE's initializer has run.
import global_before_init.early;

var LATE: i64[3] = [1, 2, 3];
fn first() -> i64 { return LATE[0]; }
fn main() { print(EARLY); }
