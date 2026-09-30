// An unqualified name resolves lexically first (§11.1): to a variable, then
// to a type parameter, then to a nested function, and only a name none of
// them has to the namespace's and the global declarations. Each of K, F, G
// and N below is also a global that the name would reach otherwise, and
// `::N` still does.
let K = 3;
let F = 5;
let G = 7;
let N = 4;

fn twice(x: i64) -> i64 { x * 2 }

// F is the function value, not the global F.
fn apply<F>(x: i64) -> i64 { F(x) }

// A type parameter hides the global of its name, which `::N` still names, in
// a signature's size as well.
fn global_n<N>(a: i64[::N], x: N) -> i64 { ::N * 10 + a.len + x }

// A local hides a type parameter of its name.
fn shadowed<F>(x: i64) -> i64 {
    let F = 10;
    F + x
}

struct S<N> { a: u8[::N], b: N }

fn main() {
    // A nested function hides the global K as a value and as a callee.
    fn K(x: i64) -> i64 { x * 10 }
    print(apply(2, K), " ", K(1), " ", apply(3, twice));
    print(global_n([1, 2, 3, 4], 1), " ", shadowed(1, twice));
    let s = S<i8> { a: [1, 2, 3, 4], b: -1 };
    print(s.a.len, " ", s.b);
    // Inside call, its type parameter G hides main's G, in the block
    // written there too.
    let G = 8;
    fn call<G>(x: i64) -> i64 { G(x) + apply(x) { G(it) } }
    print(call(3, twice), " ", G);
    // A variable comes before a nested function of its name, even one
    // declared in a scope inside the variable's.
    let H = 9;
    {
        fn H(x: i64) -> i64 { x }
        print(H, " ", F);
    }
}
