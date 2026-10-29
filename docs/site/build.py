#!/usr/bin/env python3
"""Build the documentation site with pandoc.

The site keeps the repository's layout: README.md becomes index.html, each
docs/*.md becomes docs/*.html, samples/README.md becomes samples/index.html,
and so on, so that every relative link between the files works after
links.lua turns `.md` into `.html`. Each sample program gets a highlighted
page of its own, which the links to it reach, and the files under
docs/html are copied as they are.

    python docs/site/build.py --out build/site --check

needs pandoc 2.11 or later on PATH (`--pandoc` names one elsewhere).
`--check` verifies every internal link and fragment of the built site and
fails the build on a broken one, which is how the Pages workflow runs it.
"""
import argparse
import glob
import html
import os
import re
import shutil
import subprocess
import sys
from html.parser import HTMLParser
from urllib.parse import unquote

SITE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(SITE))

# Markdown pages, as (source relative to the repository, output page).
PAGES = []
PAGES.append(("README.md", "index.html"))
PAGES.append(("docs/index.md", "docs/index.html"))
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "*.md"))):
    name = os.path.basename(src)
    if name != "index.md":
        PAGES.append(("docs/" + name, "docs/" + name[:-3] + ".html"))
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "design", "*.md"))):
    name = os.path.basename(src)
    PAGES.append(("docs/design/" + name, "docs/design/" + name[:-3] + ".html"))
PAGES.append(("docs/site/README.md", "docs/site/index.html"))
PAGES.append(("samples/README.md", "samples/index.html"))
for src in sorted(glob.glob(os.path.join(ROOT, "bench", "*.md"))):
    name = os.path.basename(src)
    PAGES.append(("bench/" + name, "bench/" + name[:-3] + ".html"))
PAGES.append(("vscode/README.md", "vscode/index.html"))
PAGES.append(("test/goose_in_goose/README.md", "test/goose_in_goose/index.html"))

# Source files shown as highlighted pages, and files copied as they are.
SOURCES = sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                 for p in glob.glob(os.path.join(ROOT, "samples", "*.goose")))
SOURCES.append("LICENSE")
COPIES = sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                for p in glob.glob(os.path.join(ROOT, "docs", "logo", "*.png")))
COPIES.append("vscode/goose-language.vsix")
# The raw results the benchmark reports link to.
COPIES += sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                 for p in glob.glob(os.path.join(ROOT, "bench", "*.json")))
# Finished HTML pages, and anything they load, are published untouched.
COPIES += sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                 for p in glob.glob(os.path.join(ROOT, "docs", "html", "**", "*"), recursive=True)
                 if os.path.isfile(p))


def title_of(md_text, fallback):
    for line in md_text.split("\n"):
        m = re.match(r"^#\s+(.*)", line)
        if m:
            t = re.sub(r"`", "", m.group(1)).strip()
            return t
    return fallback


def pandoc(args, pandoc_exe, stdin_text=None, src=None, out=None):
    cmd = [pandoc_exe, "--from", "gfm", "--to", "html5", "--standalone",
           "--template", os.path.join(SITE, "template.html"),
           "--lua-filter", os.path.join(SITE, "links.lua"),
           "--syntax-definition", os.path.join(SITE, "goose.xml"),
           "--highlight-style", "tango"] + args
    if src:
        cmd.append(src)
    if out:
        cmd += ["-o", out]
    p = subprocess.run(cmd, input=stdin_text, capture_output=True, text=True, encoding="utf-8")
    if p.returncode != 0:
        sys.stderr.write(p.stderr)
        raise SystemExit("pandoc failed on %s" % (src or out))
    for line in p.stderr.split("\n"):
        if line.strip():
            sys.stderr.write(line + "\n")
    return p.stdout


def build_page(src_rel, out_rel, out_dir, pandoc_exe):
    src = os.path.join(ROOT, src_rel)
    out = os.path.join(out_dir, out_rel)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    text = open(src, encoding="utf-8").read()
    depth = out_rel.count("/")
    root = "../" * depth
    args = ["--toc", "--toc-depth", "3",
            "--metadata", "pagetitle=" + title_of(text, os.path.basename(src_rel)),
            "--variable", "root=" + root,
            "--variable", "source=" + src_rel]
    pandoc(args, pandoc_exe, src=src, out=out)


def build_source_page(src_rel, out_dir, pandoc_exe):
    """A program or text file as one highlighted code block on a page."""
    src = os.path.join(ROOT, src_rel)
    out = os.path.join(out_dir, src_rel + ".html")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    text = open(src, encoding="utf-8").read()
    lang = "goose" if src_rel.endswith(".goose") else ""
    name = os.path.basename(src_rel)
    md = "# %s\n\n```%s\n%s\n```\n" % (name, lang, text.rstrip("\n"))
    depth = src_rel.count("/")
    args = ["--metadata", "pagetitle=" + name,
            "--variable", "root=" + "../" * depth,
            "--variable", "source=" + src_rel]
    html_text = pandoc(args, pandoc_exe, stdin_text=md)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(html_text)


class LinkScanner(HTMLParser):
    def __init__(self):
        super().__init__()
        self.hrefs = []
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        d = dict(attrs)
        if "id" in d:
            self.ids.add(d["id"])
        if tag == "a" and "name" in d:
            self.ids.add(d["name"])
        if tag == "a" and "href" in d:
            self.hrefs.append(d["href"])
        if tag in ("img", "link", "script") and (d.get("src") or d.get("href")):
            self.hrefs.append(d.get("src") or d.get("href"))


def check(out_dir):
    pages = {}
    for dirpath, _, files in os.walk(out_dir):
        for f in files:
            if f.endswith(".html"):
                path = os.path.join(dirpath, f)
                s = LinkScanner()
                s.feed(open(path, encoding="utf-8").read())
                pages[os.path.normpath(path)] = s
    bad = 0
    for path, s in pages.items():
        for href in s.hrefs:
            if re.match(r"^[a-z][a-z0-9+.-]*:", href):
                continue
            target, _, frag = href.partition("#")
            frag = html.unescape(frag)
            if target == "":
                if frag and frag not in s.ids:
                    print("%s: missing fragment #%s" % (os.path.relpath(path, out_dir), frag))
                    bad += 1
                continue
            tpath = os.path.normpath(os.path.join(os.path.dirname(path), unquote(html.unescape(target))))
            if not os.path.exists(tpath):
                print("%s: missing target %s" % (os.path.relpath(path, out_dir), href))
                bad += 1
                continue
            if frag and tpath in pages and frag not in pages[tpath].ids:
                print("%s: missing fragment %s" % (os.path.relpath(path, out_dir), href))
                bad += 1
    print("%d pages, %d broken links" % (len(pages), bad))
    return bad == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "site"), help="output directory (default build/site)")
    ap.add_argument("--pandoc", default="pandoc", help="the pandoc executable")
    ap.add_argument("--check", action="store_true", help="verify every internal link of the built site")
    a = ap.parse_args()
    out_dir = os.path.abspath(a.out)
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)
    shutil.copy(os.path.join(SITE, "style.css"), os.path.join(out_dir, "style.css"))
    for src_rel, out_rel in PAGES:
        if os.path.exists(os.path.join(ROOT, src_rel)):
            build_page(src_rel, out_rel, out_dir, a.pandoc)
    for src_rel in SOURCES:
        build_source_page(src_rel, out_dir, a.pandoc)
    for rel in COPIES:
        dst = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(os.path.join(ROOT, rel), dst)
    # GitHub Pages runs Jekyll over the artifact unless told not to.
    open(os.path.join(out_dir, ".nojekyll"), "w").close()
    print("built %d pages and %d source pages into %s" % (len(PAGES), len(SOURCES), out_dir))
    if a.check and not check(out_dir):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
