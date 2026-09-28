// error: variant North is listed twice
// A variant is named once in a match (§8.1), in a list as in its own arm.
enum Dir { North, East, South, West }

fn main() {
    let d = Dir.East;
    print(match d { North, South, North => 1, _ => 0 });
}
