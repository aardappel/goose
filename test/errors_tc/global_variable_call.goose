// error: limit is a variable, not a function
// A call naming a global variable, with no function of that name, says so.
var limit = 10;
fn main() {
    print(limit(3));
}
