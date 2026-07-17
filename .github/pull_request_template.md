## Summary

<!-- Describe what this PR changes and why. -->

## PR title (important)

Use a **conventional commit** title — it becomes the release version on merge:

| Title prefix | Release bump | Example |
|--------------|--------------|---------|
| `fix:` | patch (1.5.5 → 1.5.6) | `fix: correct Content-Length parsing` |
| `feat:` | minor (1.5.5 → 1.6.0) | `feat: add rate limiter middleware` |
| `feat!:` or `BREAKING CHANGE:` in body | major (1.5.5 → 2.0.0) | `feat!: rename cHTTPX_Parse signature` |
| `chore:`, `ci:`, `docs:` | no release tag | `chore: update workflows` |

> Use **Squash merge** so the PR title becomes the commit on `main`.

## Checklist

- [ ] PR title follows the table above
- [ ] Code builds on Linux (`make lin` and `make libchttpx.so`)
- [ ] Code builds on Windows (`make win`) if applicable
- [ ] Source is formatted (`make lin-format` or `clang-format`)
- [ ] README updated if public API or usage changed

## Related issues

<!-- Closes #123 -->

<!-- libchttpx-preview -->
<!-- /libchttpx-preview -->
