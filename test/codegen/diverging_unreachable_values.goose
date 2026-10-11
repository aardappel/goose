// abort and exit end execution before subsequent values reach any destination.
// Those values still resolve during checking, but do not constrain a return.
import os;
enum Maybe<T> { None, Some { value: T } }

fn after_abort(yes: bool) -> Maybe<i64> {
    let absent = Maybe<i64>.None {};
    if yes { return Maybe<i64>.Some { value: 7 }; }
    abort("unavailable");
    return absent;
}

fn after_exit(yes: bool) -> Maybe<i64> {
    let absent = Maybe<i64>.None {};
    if yes { return Maybe<i64>.Some { value: 9 }; }
    exit(5);
    return absent;
}

fn tail_after_abort(yes: bool) -> Maybe<i64> {
    let absent = Maybe<i64>.None {};
    if yes { return Maybe<i64>.Some { value: 11 }; }
    abort("unavailable tail");
    absent
}

fn varmode_after_exit(yes: bool) -> Maybe<i64>.. {
    let present = Maybe<i64>.Some { value: 13 };
    if yes { return present; }
    exit(6);
    return present;
}

fn reachable(yes: bool) -> Maybe<i64> {
    let absent = Maybe<i64>.None {};
    let present = Maybe<i64>.Some { value: 17 };
    if yes { return present; }
    return absent;
}

fn main() {
    let yes = args().len == 1;
    print(after_abort(yes));
    print(after_exit(yes));
    print(tail_after_abort(yes));
    print(varmode_after_exit(yes));
    print(reachable(true), " ", reachable(false));
}
