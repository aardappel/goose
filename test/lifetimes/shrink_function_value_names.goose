// A function value's body runs in the middle of a statement of the function
// calling it (§5.1, §7.6). A variable of that function, or of the one that
// wrote the value, is used after a shrink in the body only where code that
// can name it names it again: a later variable of the same name in the
// other function is another one.
var gs: i64[>..<] = [1, 2];
var go: i64[>..] = [1, 2];

// Each view is used for the last time before F() runs the block, and the
// only later w is the block's own.
fn view_gs<F>() -> i64 {
    let w = gs[..];
    print(w);
    let n = F();
    return n;
}
fn view_go<F>() -> i64 {
    let w = go[..];
    print(w);
    let n = F();
    return n;
}

// A w of their own after F() runs the block, which main's w has no part in.
fn later_gs<F>() -> i64 {
    let n = F();
    let w = 7;
    return n + w;
}
fn later_go<F>() -> i64 {
    let n = F();
    let w = 8;
    return n + w;
}

fn main() {
    print(view_gs() { gs.clear(); let w = 5; w }, " ", gs.len);
    print(view_go() { go.clear(); let w = 6; w }, " ", go.len);
    gs.push(3);
    go.push(4);
    {
        let w = gs[..];
        print(w);
        print(later_gs() { gs.clear(); 1 }, " ", gs.len);
    }
    {
        let w = go[..];
        print(w);
        print(later_go() { go.clear(); 2 }, " ", go.len);
    }
}
