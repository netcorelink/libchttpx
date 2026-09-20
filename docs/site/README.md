# libchttpx website

Static project website for GitHub Pages.

## Files

- `index.html` — landing page
- `docs.html` — quick start and local module navigation
- `module.html` / `module.js` — full module documentation rendered inside the website
- `content/*.md` — generated at deploy time from `docs/<module>/README.md`; these files are not stored in git
- `examples.html` — copyable examples
- `blog.html` — project updates and release links
- `support.html` — community, commercial and sponsorship support
- `about.html` — project information
- `styles.css` — shared styles
- `app.js` — mobile navigation, copy buttons, docs filter and GitHub repository stats
- `styles.css` — shared styles and DM Sans typography

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
