# libchttpx website

Static project website for GitHub Pages.

The documentation layout follows a Mongoose-style docs UI: sticky header,
collapsible left sidebar with search, chevron breadcrumbs, and dark code blocks.

## Files

- `index.html` — landing page
- `docs.html` — quick start and local module navigation
- `module.html` / `module.js` — full module documentation rendered inside the website
- `content/*.md` — generated at deploy time from `docs/<module>/README.md`; these files are not stored in git
- `examples.html` — copyable examples
- `blog.html` — project updates and release links
- `support.html` — community, commercial and sponsorship support
- `about.html` — project information
- `styles.css` — shared documentation layout (sidebar, breadcrumbs, dark code)
- `app.js` — navigation, sidebar, copy buttons, syntax highlighting and GitHub stats

The site has no framework. GitHub Pages has a small build step that recreates `docs/site/content` from the canonical module READMEs under `docs/`, so website documentation cannot drift from repository documentation.

For a local preview with module content, generate the same files first:

```bash
rm -rf docs/site/content
mkdir -p docs/site/content
for readme in docs/*/README.md; do
  module_name="$(basename "$(dirname "$readme")")"
  [ "$module_name" = "site" ] && continue
  cp "$readme" "docs/site/content/${module_name}.md"
done
```

Then serve the directory.

For a quick local preview:

```bash
cd docs/site
python3 -m http.server 8080
```

Then open `http://localhost:8080`.
