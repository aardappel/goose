// error: function twice has ensures and ends in a value: write its result type
// An ensures clause needs to know whether the body's tail is the result.
fn twice(x: i64)
    ensures x >= 0
{
    x * 2
}

fn main() {
    print(twice(3));
}
