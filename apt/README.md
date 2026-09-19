# libchttpx APT repository

This directory is the APT repository for Debian/Ubuntu users.

Repository URL:

```text
https://raw.githubusercontent.com/netcorelink/libchttpx/packages/apt
```

## Install for users

Add the repository once:

```bash
curl -fsSL https://raw.githubusercontent.com/netcorelink/libchttpx/packages/apt/libchttpx.sources \
  | sudo tee /etc/apt/sources.list.d/libchttpx.sources >/dev/null

sudo apt update
```

Then install:

```bash
sudo apt install libchttpx-dev
```

Compile an application:

```bash
gcc server.c -o server $(pkg-config --cflags --libs libchttpx)
```

Update later with the normal system update flow:

```bash
sudo apt update
sudo apt upgrade
```

Remove the package:

```bash
sudo apt remove libchttpx-dev
```

Remove the repository itself:

```bash
sudo rm /etc/apt/sources.list.d/libchttpx.sources
sudo apt update
```

## Trust model

The current repository intentionally uses `Trusted: yes`, so users do not need
to install a separate GPG key. This is convenient, but it disables APT package
signature verification for this repository.

For a production-grade public repository, migrate to a signed `InRelease`
repository and replace `Trusted: yes` with `Signed-By`.
