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
2. Open **Actions → Publish APT Package**.
3. Choose the `packages` branch if GitHub asks which workflow version to run.
4. Enter the tag, for example `v1.6.0`.
5. Run the workflow.

The workflow checks out that tag, builds `libchttpx-dev_<version>_amd64.deb`,
updates APT metadata, commits the package back to the `packages` branch, and
also uploads the `.deb` to the matching GitHub Release when one exists.

The package repository itself is served directly from the branch through
`raw.githubusercontent.com`, so it does not interfere with the documentation
site deployed through GitHub Pages.
