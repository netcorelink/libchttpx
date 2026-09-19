# Package publishing

The `packages` branch is a long-lived package-storage branch.

APT repository files live under:

```text
apt/
├── libchttpx.sources
├── dists/
│   └── stable/
│       ├── Release
│       └── main/
│           └── binary-amd64/
│               ├── Packages
│               └── Packages.gz
└── pool/
    └── main/
        └── l/
            └── libchttpx/
                └── libchttpx-dev_<version>_amd64.deb
```

## Publish a version

1. Create/publish the normal libchttpx GitHub release/tag, for example `v1.6.0`.
2. Change `publish/version.txt` in this branch to that tag.
3. Push the change to `packages`.

That push starts **Publish APT Package** automatically.

The workflow checks out the requested tag, builds
`libchttpx-dev_<version>_amd64.deb`, updates APT metadata, commits the package
back to the `packages` branch, uploads the `.deb` to the matching GitHub
Release when one exists, and asks the GitHub Pages workflow to refresh the site.

`workflow_dispatch` is also defined so the same workflow can be reused from
the default branch later if desired.

The package repository itself is served directly from the branch through
`raw.githubusercontent.com`, so it does not interfere with the documentation
site deployed through GitHub Pages.
