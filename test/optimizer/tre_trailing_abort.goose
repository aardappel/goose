// Tail-recursive functions whose bodies end in abort or exit: each becomes a
// loop with nothing after that last statement, which control never gets past.
// A leading local keeps base-case inlining from rewriting the self calls first.
recursive fn sum(n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return 0; }
    if n > 0 { return n + sum(n - 1); }
    abort("negative");
}
recursive fn count(n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return 0; }
    if n > 0 { return 1 + count(n - 1); }
    exit(3);
}
// Ends in a block that ends in abort.
recursive fn twice(n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return 0; }
    if n > 0 { return 2 + twice(n - 1); }
    { print("negative"); abort("negative"); }
}
fn main() {
    print(sum(10), " ", count(5), " ", twice(4));
}
