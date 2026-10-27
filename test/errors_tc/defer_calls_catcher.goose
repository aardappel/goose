// error: defer body calls cleanup, which calls exception::try, which calls exception::handler, which calls risky, which uses a long-distance return (a `return ... from` site, a frame one passes through, or a catch, §7.9): a defer body runs while such a return may be in flight (§6.6)
// A contained throw and catch inside what the body calls writes the same
// in-flight state as a return passing through the frame, so it is rejected
// too, naming the chain of calls that brings it in.
import std;
import exception;
fn risky() { exception::throw("oops"); }
fn cleanup() {
    exception::try<u8[]>(risky) { e => print("swallowed ", e); };
}
fn work() -> i64 {
    defer cleanup();
    return 1;
}
fn main() { print(work()); }
