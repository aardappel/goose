// runtime-debug: reference members compare target identity, not offsets.
struct Node { value: i64, next: Node&<u32>? }
struct Link { next: Nested&<u16>? }
struct Nested { value: i64, link: Link }
struct Links { refs: (ArrayNode&<u32>?)[1] }
struct ArrayNode { value: i64, links: Links }
struct LimitedNode { value: i64, refs: (LimitedNode&<u32>?)[..2] }
fn identity<T>(n: T&) -> T& { n }

fn main() {
    var ns: Node[>..] = [];
    ns.push(Node { value: 2, next: null });
    ns.push(Node { value: 2, next: ns[0] });
    ns.push(Node { value: 2, next: ns[0] });
    ns.push(Node { value: 2, next: ns[2] });
    ns.push(Node { value: 2, next: null });
    ns.push(Node { value: 3, next: ns[0] });
    print("targets ", ns[1].next .== ns[2].next, " ", ns[1].next .== ns[3].next);
    print("records ", ns[1] == ns[2], " ", ns[1] == ns[3], " ", ns[1] != ns[2], " ", ns[1] != ns[3]);
    print("calls ", ns[1] == identity(ns[2]), " ", ns[1] == identity(ns[3]), " ", identity(ns[1]) == ns[2]);
    print("controls ", ns[0] == ns[4], " ", ns[1] == ns[1], " ", ns[1] == ns[4], " ", ns[4] == ns[1], " ", ns[1] == ns[5]);
    print("slices ", ns[1..2] == ns[2..3], " ", ns[1..2] == ns[3..4], " ", ns[1..2] == ns[1..3]);
    print("ranges ", ns == ns, " ", ns == ns[1..2]);
    ns[1].next .= ns[3];
    ns[2].next .= ns[3];
    print("forward ", ns[1].next .== ns[2].next, " ", ns[1] == ns[2]);

    var nested: Nested[>..] = [];
    nested.push(Nested { value: 2, link: Link { next: null } });
    nested.push(Nested { value: 2, link: Link { next: nested[0] } });
    nested.push(Nested { value: 2, link: Link { next: nested[0] } });
    nested.push(Nested { value: 2, link: Link { next: nested[2] } });
    print("nested ", nested[1].link.next .== nested[2].link.next, " ", nested[1].link.next .== nested[3].link.next, " ", nested[1] == nested[2], " ", nested[1] == nested[3]);
    print("nested calls ", nested[1] == identity(nested[2]), " ", identity(nested[1]) == nested[3]);

    var arrays: ArrayNode[>..] = [];
    arrays.push(ArrayNode { value: 2, links: Links { refs: [null] } });
    arrays.push(ArrayNode { value: 2, links: Links { refs: [arrays[0]] } });
    arrays.push(ArrayNode { value: 2, links: Links { refs: [arrays[0]] } });
    arrays.push(ArrayNode { value: 2, links: Links { refs: [arrays[2]] } });
    print("arrays ", arrays[1].links.refs[0] .== arrays[2].links.refs[0], " ", arrays[1].links.refs[0] .== arrays[3].links.refs[0], " ", arrays[1] == arrays[2], " ", arrays[1] == arrays[3]);
    print("ref arrays ", arrays[1].links.refs == arrays[2].links.refs, " ", arrays[1].links.refs == arrays[3].links.refs);
    print("array calls ", arrays[1].links.refs == identity(arrays[2].links.refs), " ", identity(arrays[1].links.refs) == arrays[3].links.refs);

    var limited: LimitedNode[>..] = [];
    limited.push(LimitedNode { value: 2, refs: [] });
    limited.push(LimitedNode { value: 2, refs: [limited[0], null] });
    limited.push(LimitedNode { value: 2, refs: [limited[0], null] });
    limited.push(LimitedNode { value: 2, refs: [limited[2], null] });
    print("limited ", limited[1].refs[0] .== limited[2].refs[0], " ", limited[1].refs[0] .== limited[3].refs[0], " ", limited[1] == limited[2], " ", limited[1] == limited[3]);
}
