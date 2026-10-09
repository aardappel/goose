// runtime-define: GS_STACK_STATS=1
// Under GS_STACK_STATS a thread program reports on stderr, as it ends, the
// data stacks and regions it took: the stacks only ever grow, so their
// count at the end is the most it had. The program's own output is as
// without it.

fn longest(parts: u8[>..]&, extra: u8[]) -> i64 {
    var joined: u8[>..] = [];
    joined.append(parts[..]);
    joined.append(extra);
    return joined.len;
}

fn main() {
    var parts: u8[>..] = [];
    parts.append("goose");
    print(longest(parts, "!"));         // 6
    print(str(42).len);                 // 2
}
