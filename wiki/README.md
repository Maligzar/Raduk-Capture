# Wiki source

The GitHub wiki for this repository, kept here under version control.

26 pages, ~6,600 lines. `Home.md` is the landing page; `_Sidebar.md` and
`_Footer.md` are GitHub wiki special pages that render on every page.

## Publishing

A GitHub wiki is a separate git repository (`<repo>.wiki.git`) with **no
REST API**, so publishing is a clone-copy-commit-push:

```bash
./wiki/publish-wiki.sh --dry-run   # show what would change
./wiki/publish-wiki.sh             # publish
```

The script removes pages the wiki has that `wiki/` no longer defines, so a
rename here does not leave an orphan page behind.

**The wiki must already exist.** If it has never been initialised, create
one page through the web UI first:
<https://github.com/Maligzar/Raduk-Capture/wiki>

Override the target with `WIKI_REPO` or `WIKI_URL` if needed.

## Editing

Pages are plain GitHub-flavoured Markdown. Conventions:

- **Links between pages** use the page name without `.md`:
  `[Capture Pipeline](Capture-Pipeline)`. Anchors use the GitHub slug:
  `[…](Image-Analysis#sharpness)`.
- **Source references** link to the file on `master`:
  `` [`src/core/RingBuffer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/RingBuffer.h) ``
  Line-numbered references use `#L106`.
- **Blockquote callouts** carry design rationale, usually lifted from the
  source comments. That reasoning is the most valuable content in the wiki;
  keep it when editing around it.
- **Mermaid diagrams** render on GitHub wikis.

Adding a page means adding it to `Home.md` **and** `_Sidebar.md`.

## Checking links before publishing

```bash
python3 - <<'PY'
import os, re
pages = {f[:-3] for f in os.listdir('wiki') if f.endswith('.md')}
anchors = {}
for f in os.listdir('wiki'):
    if not f.endswith('.md'): continue
    a = set()
    for h in re.findall(r'^#{1,6}\s+(.+)$', open('wiki/'+f).read(), re.M):
        s = h.lower()
        s = re.sub(r'[`*_\[\]()]', '', s)
        s = re.sub(r'[^a-z0-9\s-]', '', s)
        a.add(re.sub(r'\s+', '-', s.strip()))
    anchors[f[:-3]] = a
bad = []
for f in os.listdir('wiki'):
    if not f.endswith('.md'): continue
    for m in re.finditer(r'\[([^\]]+)\]\(([^)]+)\)', open('wiki/'+f).read()):
        t = m.group(2)
        if t.startswith(('http', '#', 'mailto')): continue
        page, _, anc = t.partition('#')
        if page and page not in pages: bad.append((f, t, 'page'))
        elif anc and anc not in anchors.get(page or f[:-3], set()): bad.append((f, t, 'anchor'))
print('broken:', bad or 'none')
PY
```

## Sources

The wiki is derived from the source tree plus:

- [`RadukCapture-Architecture.md`](../RadukCapture-Architecture.md)
- [`RadukCapture-Help.md`](../RadukCapture-Help.md)
- [`docs/`](../docs) — code reviews, security review, tracking findings, phase plans

Where those documents and the code disagree, the wiki follows the code and
says so. Known divergences are noted in
`Roadmap-and-Phase-History.md` (where the version is declared) and
`Architecture-Overview.md` (the `PulseGuide` fire-and-forget claim).
