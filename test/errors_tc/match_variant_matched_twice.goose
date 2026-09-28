// error: variant South is already matched by an earlier arm
// A variant is named once in a match (§8.1): an arm that lists it again
// could never match it.
enum Dir { North, East, South, West }

fn main() {
    let d = Dir.East;
    print(match d { North, South => 1, East, South => 2, West => 3 });
}
