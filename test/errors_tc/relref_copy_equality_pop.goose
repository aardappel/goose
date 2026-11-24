// error: its links, read out of this copy, would be measured from the copy
enum List { End, Link { next: List.Link&<u32>? } }

fn main() {
    var links: List.Link[>..] = [];
    links.push(List.Link { next: null });
    print(links.pop() == List.Link { next: null });
}
