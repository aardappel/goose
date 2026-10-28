#!/usr/bin/env python3
import json
import os
import re
import glob
import hashlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SITE = os.path.dirname(os.path.abspath(__file__))

# Copy logic from build.py
pages = []
pages.append(("README.md", "index.html"))
pages.append(("docs/index.md", "docs/index.html"))
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "*.md"))):
    name = os.path.basename(src)
    if name != "index.md":
        pages.append(("docs/" + name, "docs/" + name[:-3] + ".html"))
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "design", "*.md"))):
    name = os.path.basename(src)
    pages.append(("docs/design/" + name, "docs/design/" + name[:-3] + ".html"))
pages.append(("docs/site/README.md", "docs/site/index.html"))
pages.append(("samples/README.md", "samples/index.html"))
for src in sorted(glob.glob(os.path.join(ROOT, "bench", "*.md"))):
    name = os.path.basename(src)
    pages.append(("bench/" + name, "bench/" + name[:-3] + ".html"))
pages.append(("vscode/README.md", "vscode/index.html"))
pages.append(("test/goose_in_goose/README.md", "test/goose_in_goose/index.html"))

def slug(h):
    # Matches pandoc's gfm auto identifiers
    h = re.sub(r'<[^>]+>', '', h).lower()
    h = re.sub(r'[^\w\- ]', '', h)
    return h.strip().replace(' ', '-')


def plain(text):
    text = re.sub(r'!\[([^\]]*)\]\([^)]*\)', r'\1', text)
    text = re.sub(r'\[([^\]]+)\]\([^)]*\)', r'\1', text)
    text = re.sub(r'<[^>]+>', ' ', text)
    text = re.sub(r'[`*~|]', ' ', text)
    return re.sub(r'\s+', ' ', text).strip()


def build_index(out_dir):
    search_index = []
    for src_rel, out_url in pages:
        src_path = os.path.join(ROOT, src_rel)
        if not os.path.exists(src_path):
            continue
        with open(src_path, encoding='utf-8') as f:
            lines = f.read().split('\n')

        page_title = os.path.basename(src_rel).replace('.md', '')
        for line in lines:
            m = re.match(r'^#\s+(.*)', line)
            if m:
                page_title = plain(m.group(1))
                break

        # One entry per section, code blocks included
        sections = [(page_title, "", [])]
        in_code = False
        for line in lines:
            if line.startswith("```"):
                in_code = not in_code
            m = None if in_code else re.match(r'^(#{1,4})\s+(.*)', line)
            if m:
                sections.append((plain(m.group(2)), slug(m.group(2)), []))
            else:
                sections[-1][2].append(line)

        for heading, anchor, body in sections[1:] if len(sections) > 1 and sections[0][2] == [] else sections:
            text = plain("\n".join(body))
            if not text and not anchor:
                continue
            search_index.append({
                'title': page_title,
                'heading': heading if anchor else "",
                'url': out_url + ('#' + anchor if anchor else ''),
                'content': text,
            })

    data = json.dumps(search_index, separators=(',', ':')).encode('utf-8')
    # Content hash in the name lets the browser cache the file forever
    name = 'search.%s.json' % hashlib.sha256(data).hexdigest()[:10]
    with open(os.path.join(out_dir, name), 'wb') as f:
        f.write(data)
    print(f'Generated {name} with {len(search_index)} entries')
    return name
