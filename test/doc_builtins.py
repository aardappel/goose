"""Every builtin the compiler knows is named where a reader looks builtins up:
in the builtins section of the language reference (docs/reference.md,
section 25) and in that of the specification (docs/goose_spec.md, §12),
whose tables point on to the sections that describe each one. The list is
the compiler's own, the table of src/builtins.h (one F(B_..., "name", ...)
entry per builtin, the array members among them), so a builtin added to the
compiler and not to the documentation fails this check. A name counts where
it is a word of an inline code span (`thread_wait(id)`, `.push`,
`pool.apply(...)`) in the section, which runs from its heading to the next
heading of its level or above.

    python test/doc_builtins.py [repository root]
"""
import os
import re
import sys

# Each document, and the heading its builtins section starts with.
DOCS = [
    ("docs/reference.md", "## 25. Builtins"),
    ("docs/goose_spec.md", "## 12. Builtins"),
]


def builtin_names(root):
    """The names in src/builtins.h's table, in its order."""
    with open(os.path.join(root, "src", "builtins.h"), encoding="utf-8") as f:
        return re.findall(r'^\s*F\(B_\w+,\s*"(\w+)"', f.read(), re.M)


def section(text, heading):
    """The section that starts with a line beginning with `heading`, up to the
    next heading of its level or above; None where there is no such line."""
    m = re.search(r"^" + re.escape(heading), text, re.M)
    if not m:
        return None
    level = len(heading) - len(heading.lstrip("#"))
    end = re.compile(r"^#{1,%d} " % level, re.M).search(text, m.end())
    return text[m.start():end.start() if end else len(text)]


def code_words(text):
    """The words of the text's inline code spans, which may run over a line
    break, fenced blocks left out."""
    text = re.sub(r"^```.*?^```", "", text, flags=re.M | re.S)
    return set(re.findall(r"\w+", " ".join(re.findall(r"`([^`]+)`", text))))


def check_doc_builtins(root):
    """The number of builtins, where both documents name every one; else
    RuntimeError naming what each leaves out."""
    names = builtin_names(root)
    if len(names) < 40:
        raise RuntimeError(f"src/builtins.h: read only {len(names)} builtins from its table")
    problems = []
    for doc, heading in DOCS:
        with open(os.path.join(root, doc), encoding="utf-8") as f:
            body = section(f.read(), heading)
        if body is None:
            problems.append(f"{doc}: no section starting {heading!r}")
            continue
        words = code_words(body)
        missing = [n for n in names if n not in words]
        if missing:
            problems.append(f"{doc}, {heading.lstrip('# ')}: no {', '.join(missing)}")
    if problems:
        raise RuntimeError("builtins of src/builtins.h the documentation does not name:\n" +
                           "\n".join(problems))
    return len(names)


if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..")
    try:
        n = check_doc_builtins(root)
    except (RuntimeError, OSError) as e:
        print(e)
        sys.exit(1)
    print(f"all {n} builtins of src/builtins.h are named in "
          + " and ".join(f"{doc} ({heading.lstrip('# ')})" for doc, heading in DOCS))
