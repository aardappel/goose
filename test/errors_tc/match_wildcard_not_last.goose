// error: _ must be the last match arm
// An arm after _ could never match (§8.1), whether the match is over an
// ADT's variants or an integer's values.
enum Dir { North, East, South, West }

fn main() {
    let d = Dir.East;
    print(match d { North => 1, _ => 0, East => 2 });
}
