// A directory tree: an N-ary tree whose child lists grow and shrink. Every
// node lives in a `reusable` pool of slots, and the program holds ordinary
// references to them, while the tree itself links them with 4-byte offsets
// from the pool's base. A directory's entries are one contiguous run of those
// links in a `reusable[]` pool of slices, so an entry list is a plain slice to
// walk. Adding an entry grows that run in place when there is room after it,
// and otherwise moves it where a new run of that size would go. Removing a
// subtree gives its slots and runs back, and the next additions take them
// again: no allocator is involved anywhere.
//
// Build and run (from this directory):
//   goose -o file_tree.c 26_file_tree.goose && cl file_tree.c && file_tree

// A link to a node: an offset into `nodes` (§3.9). The cells a run gains start
// out as the default value, null, so the link is optional, but every link
// inside a directory's run is set before anything reads it.
type Link = Node&<u32 in nodes>?;

struct Node {
    name: u8[..15],     // inline, so a Node is fixed-size, as pool elements must be
    entries: Link[:],   // the run of links to what this directory holds
}

reusable var nodes: Node[>..] = [];
reusable[] var runs: Link[>..] = [];

// A node with no entries: an empty run, placed by the first addition.
fn make(name: u8[:]) -> Node& {
    return nodes.alloc_ref(Node { name: name, entries: runs.alloc_slice(0) });
}

let root .= make("/");

// Appends a link to `n` to the entries of directory `dir`. realloc_slice
// returns where the run is now, which the directory keeps. A run reached
// through a reference is not one the checker can place in `runs`, so the
// operations that take one check that when they run.
fn link(dir: Node&, n: Node&) {
    let k = dir.entries.len;
    dir.entries = runs.realloc_slice(dir.entries, k + 1);
    dir.entries[k] .= n;
}

fn add(dir: Node&, name: u8[:]) -> Node& {
    let n .= make(name);
    link(dir, n);
    return n;
}

// Takes entry `i` out of directory `dir`: the entries after it shift down,
// and the run shrinks by one, which gives its last cell back to `runs`.
fn unlink(dir: Node&, i: i64) -> Node& {
    let n = dir.entries[i];
    assert(n);
    for j in i..dir.entries.len - 1 { dir.entries[j] .= dir.entries[j + 1]; }
    dir.entries = runs.realloc_slice(dir.entries, dir.entries.len - 1);
    return n;
}

// Where node `child` sits among the entries of `dir`. `.==` asks whether two
// references name the same node, where `==` would compare their contents.
fn position(dir: Node&, child: Node&) -> i64 {
    for i in dir.entries.len {
        let e = dir.entries[i];
        assert(e);
        if e .== child { return i; }
    }
    abort("not an entry");
}

// Gives a subtree back: its runs to `runs`, its slots to `nodes`, which
// index_of turns a node back into. A freed node keeps its bytes, so a stale
// link would still read a valid Node.
recursive fn release(n: Node&) {
    for e in n.entries {
        assert(e);
        release(e);
    }
    runs.free_slice(n.entries);
    nodes.free(nodes.index_of(n));
}

// rm -r: unlinked from its directory, then given back.
fn remove(dir: Node&, child: Node&) {
    unlink(dir, position(dir, child));
    release(child);
}

// mv: the node keeps its slot, and only the two runs change.
fn move(dir: Node&, child: Node&, to: Node&) {
    link(to, unlink(dir, position(dir, child)));
}

// One line per node, indented by depth; returns how many nodes it listed.
recursive fn show(n: Node&, depth: i64, out: u8[>..]&) -> i64 {
    for i in depth { format(out, "  "); }
    format(out, n.name, "\n");
    var count = 1;
    for e in n.entries {
        assert(e);
        count += show(e, depth + 1, out);
    }
    return count;
}

// The tree, after a line comparing what is live with what the pools hold:
// every node but the root is one link in some run.
fn listing(what: u8[:]) {
    var out: u8[>..] = [];
    let live = show(root, 0, out);
    print("-- ", what, ": ", live, " nodes in ", nodes.len, " slots, ", live - 1, " links in ",
          runs.len, " cells");
    print(out[..^1]);
}

fn main() {
    let home .= add(root, "home/");
    let ann .= add(home, "ann/");
    add(ann, "notes.txt");
    let todo .= add(ann, "todo.txt");
    let bob .= add(home, "bob/");
    add(bob, "game.sav");
    let etc .= add(root, "etc/");
    add(etc, "hosts");
    listing("built");

    // Two nodes and their runs go back to the pools...
    remove(home, bob);
    listing("rm -r /home/bob");

    // ...and new nodes and links take them again: nothing grows.
    let carl .= add(home, "carl/");
    add(carl, "a.txt");
    listing("mkdir /home/carl, touch a.txt");

    // ann's run shrinks, giving a cell back. /etc's run has no room after
    // it and no free run holds its two entries, so it moves to the end of
    // the array and leaves its old cell free as well.
    move(ann, todo, etc);
    listing("mv /home/ann/todo.txt /etc");

    // Now /etc's run ends the array and grows in place, while the next
    // entries for ann and carl fill the two free cells.
    for i in 3 { add(etc, str("log", i)); }
    add(ann, "b.txt");
    add(carl, "c.txt");
    listing("more files");
}
