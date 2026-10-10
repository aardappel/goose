// error: expected ']' in indexing, found 'i'
// Each `<` can begin either a comparison or generic arguments. Failed
// speculative parses at an inner `<` must not be repeated from every outer one.
fn main() {
    let x = [a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T[a<T i
