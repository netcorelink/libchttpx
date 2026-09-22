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
- [ ] Source follows `.clang-format` and keeps logical blocks readable
- [ ] ASan/UBSan pass; TSan passes for concurrency-sensitive changes
- [ ] Parser/network changes include negative-path tests and fuzz coverage where applicable
- [ ] Public API changes document ownership, lifetime, thread-safety, and compatibility impact
- [ ] Documentation is updated when public API or observable behavior changes

## Related issues

<!-- Closes #123 -->
