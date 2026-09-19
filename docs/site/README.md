# libchttpx website

Static project website for GitHub Pages.

## Files

- `index.html` — landing page
- `docs.html` — quick start and local module navigation
- `module.html` / `module.js` — full module documentation rendered inside the website
- `content/*.md` — website copies of the module documentation
- `examples.html` — copyable examples
- `blog.html` — project updates and release links
- `support.html` — community, commercial and sponsorship support
- `about.html` — project information
- `styles.css` — shared styles
- `app.js` — mobile navigation, copy buttons, docs filter and GitHub repository stats
- `styles.css` — shared styles and DM Sans typography

The site has no build step and no framework. Serve this directory as static files.

For a quick local preview:

```bash
cd docs/site
python3 -m http.server 8080
```

Then open `http://localhost:8080`.
