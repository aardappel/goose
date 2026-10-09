// A namespaced global variable sharing its leaf name with global functions:
// a call in the namespace names those functions (§11.1).
namespace tally;
import std;

var count: i64 = 0;
var last: i64 = -1;

fn add(xs: i64[:]) {
    count += count(xs) { it > 0 };
    last = last(xs);
}
