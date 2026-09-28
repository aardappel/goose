// error: match pattern EWOULDBLOCK (11) never matches
// Two names for one value in separate arms leave the second arm nothing to
// match (§8.1); the diagnostic shows the value, and one arm listing both
// names would take either.
let EAGAIN = 11;
let EWOULDBLOCK = 11;

fn main() {
    var e = 11;
    print(match e { EAGAIN => "retry", EWOULDBLOCK => "wait", _ => "fail" });
}
