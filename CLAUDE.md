# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Gargoyle is a third-party router firmware built as a web-based front end on top of OpenWrt. It is not a package that installs onto stock OpenWrt — it's a full firmware build system that downloads a pinned OpenWrt source tree, overlays Gargoyle-specific packages/patches/config on top, and produces flashable images plus standalone `.ipk` packages for several hardware targets (ath79, bcm27xx, bcm47xx, ipq40xx, ipq806x, mediatek, mvebu, ramips, rockchip, x86).

The current OpenWrt base is pinned in `build.sh` (`branch_name`, `openwrt_commit`) — check there before assuming which OpenWrt release/commit is in play.

## Build commands

Builds are driven through the top-level `Makefile`, which shells out to `build.sh` (full/clean build) or `rebuild.sh` (packages-only, reuses an existing `<target>-src` tree).

```sh
# Build every target from scratch (downloads OpenWrt source, applies patches, builds images)
make FULL_BUILD=true

# Build a single target (e.g. ath79) from scratch
make ath79 FULL_BUILD=true

# Build a single target+profile (e.g. ath79 "nand" profile)
make ath79.nand FULL_BUILD=true

# Rebuild just Gargoyle packages for a target without rebuilding OpenWrt/kernel
make ath79

# Build a custom target (device not covered by a stock target); see wiki for details
make custom

# Remove build artifacts but keep downloaded OpenWrt source
make clean

# Remove everything, including downloaded OpenWrt source and compressed JS/CSS caches
make distclean
```

Valid target names are the directory names under `targets/` (`ath79`, `bcm27xx`, `bcm47xx`, `ipq40xx`, `ipq806x`, `mediatek`, `mvebu`, `ramips`, `rockchip`, `x86`, `custom`). A `<target>.<profile>` form selects one of the profiles under `targets/<target>/profiles/`.

Output locations after a build:
- `<target>-src/` — the working OpenWrt + Gargoyle source tree for that target (regenerated each full build)
- `built/<target>/<profile>/` — built `.ipk` packages
- `images/<target>/` — flashable firmware images
- `downloaded/` — cached OpenWrt/package git checkouts (kept across builds; deleted by `distclean`)

There is no unit test suite / linter to run in this repo — verification is done by building a target and testing on real or emulated hardware. `dev-utils/GenLicences.sh` and `dev-utils/do_release.sh` are release-time utilities, not part of normal dev iteration.

### Build script parameters

`build.sh` and `rebuild.sh` take the same positional-argument shape, invoked by the Makefile: target, version string, verbosity, custom target/template, JS/CSS compression flags, profile, translation type (`internationalize`/`localize`), fallback/active language, build thread count, distribution flag. Prefer going through `make` with the documented variables (`FULL_BUILD`, `JS_COMPRESS`, `CSS_COMPRESS`, `TRANSLATION`, `FALLBACK_LANG`, `ACTIVE_LANG`, `BUILD_THREADS`, `CUSTOM_TARGET`, `CUSTOM_TEMPLATE`, `DISTRIBUTION`) rather than calling the scripts directly.

## Architecture

### Build pipeline (`build.sh`)

For each target, `build.sh`:
1. Downloads/reuses a pinned OpenWrt source checkout into `downloaded/<branch>-<commit>`, then **copies** it to `<target>-src/` (each target gets its own full copy, not a shared tree).
2. Copies every directory in `package/` (or `package-prepare/` if present — see below) into `<target>-src/package`, overwriting any same-named upstream OpenWrt package.
3. Copies the target's `.config` from `targets/<target>/profiles/<profile>/config` into `<target>-src/.config`.
4. Runs `i18n-scripts/internationalize.py` or `localize.py` to bake in translations (falls back to `finalize_tran_ltd.sh`/`intl_ltd.sh` shell versions if Python isn't available).
5. Optionally minifies JS/CSS under `compressed_javascript/` / `compressed_css/` using `terser`/`uglifycss` (installed via npm into `minifiers/` if missing).
6. Runs OpenWrt's own `make` inside `<target>-src` to actually build the image and packages.
7. Copies resulting `.ipk`s into `built/<target>/<profile>/`, and matching firmware images (per `targets/<target>/profiles/<profile>/profile_images`) into `images/<target>/`.

`rebuild.sh` mirrors this but skips re-downloading/re-copying OpenWrt — it assumes `<target>-src` already exists and just refreshes Gargoyle's own packages/config before invoking `make` again. Use it for iterating on Gargoyle package code without paying the full OpenWrt rebuild cost.

`package-prepare/` (present in the working tree but untracked) acts as an override for `package/` during a build — if it exists, its contents are copied instead of `package/`. This is a staging area, not something to edit directly unless you're specifically working with generated/preprocessed package variants.

### Package layout (`package/`)

Each subdirectory of `package/` is an OpenWrt-style package (`Makefile` + `files/` and/or `src/`), following standard OpenWrt package conventions (`Package/<name>` define blocks, `DEPENDS`, `Build/Prepare`, `Build/Compile`, etc.). Notable ones:

- **`package/gargoyle/`** — the core web UI package. `src/` has small C helpers (`gargoyle_header_footer.c`, `gargoyle_session_validator.c`) used by every page; `files/www/` is the actual CGI web application (see below); `files/etc/` and `files/usr/` hold init scripts and shared shell libraries installed onto the router.
- **`gargoyle-firewall-util`** / **`gargoyle-firewall-util-fw3`** — firewall abstraction layer Gargoyle's UI drives; there are separate variants for nftables (`fw4`-era) vs. legacy `fw3`/iptables.
- **`gargoyle-extras`**, **`gargoyle-profiles`**, **`gargoyle-i18n`**, **`gargoyle-ip-query`**, **`gargoyle-mjpg-streamer`**, **`gargoyle-tor`**, **`bwmon-gargoyle`**, **`webmon-gargoyle`** (implied by deps) — feature packages layered on top of the core.
- **`gpkg`** — Gargoyle's package manager UI/tooling (distinct from opkg).
- Everything else in `package/` is either a locally-maintained fork of an upstream package (patched for Gargoyle compatibility) or a dependency not present in stock OpenWrt feeds. See `CONTRIBUTING.md`'s "Package Conventions" for when something belongs here vs. being referenced from upstream feeds.

### Web UI (`package/gargoyle/files/www/`)

Pages are `haserl` CGI scripts (`#!/usr/bin/haserl`, embedded `<% ... %>` shell blocks that emit HTML) — this is a shell-templated CGI app, not a JS/backend framework. Every page starts with the same two calls:
- `gargoyle_session_validator` — validates the session cookie, redirects to `login.sh` on failure.
- `gargoyle_header_footer` — emits the shared page chrome, given a section/page id and list of JS/CSS assets to include.

`templates/` holds reusable HTML fragment templates referenced by multiple pages; `data/` holds static reference data (timezones, country/APN lists); `js/` and `themes/` hold client-side assets. `<%~ Key %>` markers are i18n string lookups resolved by the `i18n-scripts/` translation pipeline at build time — see `finalize_translation.py` for how a language is baked into a build (`internationalize` bundles a language switcher + selected languages; `localize` bakes in exactly one language and strips i18n machinery).

### Targets and profiles (`targets/`)

Each `targets/<name>/` corresponds to an OpenWrt target architecture and contains one or more `profiles/<profile>/config` (a full OpenWrt `.config`) plus a `profile_images` file listing which built image filenames belong to that profile. Per `CONTRIBUTING.md`, targets must stay *generic* (`CONFIG_TARGET_ath79=y`), not pinned to one device (`CONFIG_TARGET_ath79_generic_DEVICE_...=y`) — device-specific selection belongs in the OpenWrt image builder/menuconfig flow, not hardcoded into a target's checked-in config.

### Patches (`patches-generic/`, `netfilter-match-modules/`)

`patches-generic/*.patch` are applied to the OpenWrt source tree at build time (numbered, applied in order) — this is how Gargoyle changes upstream OpenWrt behavior without forking OpenWrt wholesale. `netfilter-match-modules/` is the other major source-tree change; see "Biggest departures from vanilla OpenWrt" below.

### Internationalization (`i18n-scripts/`)

Translation strings live alongside each plugin per `CONTRIBUTING.md` conventions (language plugins are named `plugin-gargoyle-i18n-<Language>-<ISO 639-1>`, e.g. `English-EN`). The Python scripts (`internationalize.py`, `localize.py`, `finalize_translation.py`) are the primary implementation; the `.sh` variants (`intl_ltd.sh`, `finalize_tran_ltd.sh`) are a fallback path used only when Python is unavailable on the build host.

### Biggest departures from vanilla OpenWrt

These three pieces have no upstream equivalent and are where Gargoyle differs most from stock OpenWrt. Changes to them can't be checked against upstream; they have to be tested on a router.

- **Custom netfilter match modules (`netfilter-match-modules/`).** Four kernel match modules, each with an iptables (`xt_*`) and an nftables (`nft_*`) implementation: `bandwidth` (byte counting and quotas, used by bwmon/quotas via `libiptbwctl`/`libnftbwctl`), `timerange` (time-of-day matching for restrictions and quotas), `webmon` (records visited domains and searches for webmon-gargoyle), and `weburl` (URL/domain matching for access restrictions).
  - Each module directory holds the kernel source (`module/`), headers (`header/`), the userspace part (an iptables `extension/`, or `nftables/` + `libnftnl/` for nft), and a `name` file.
  - `integrate_netfilter_modules.sh` doesn't build anything itself. It extracts pristine copies of the target's kernel, iptables, nftables and libnftnl sources, adds the modules to them, and writes the result as generated patches into the OpenWrt tree. The kernel changes become `650-custom_netfilter_match_modules.patch` in the target's kernel patch dir, along with matching patches for the iptables/nftables/libnftnl packages. It also appends `kmod-*` definitions to `package/kernel/linux/modules/netfilter.mk` and `include/netfilter.mk`.
  - `build.sh` runs it twice, in two modes: `1 0` (OpenWrt package/makefile changes) before `make defconfig`, and `0 1` (kernel/userspace source patches) after. That's why a kernel or OpenWrt version bump can break these modules even when no Gargoyle code changed.
- **`gpkg` (`package/gpkg/`).** Gargoyle's own C package manager, compatible with opkg's package format and `/etc/opkg.conf`. It replaces opkg for plugin management. It's a dependency of the core `gargoyle` package and is what the web UI (`plugins.sh`, `/usr/lib/gargoyle/install_gargoyle_package.sh`) calls.
  - It's built on Gargoyle's own `libericstools`, `libbbtargz` and `ewget`, not on opkg's libraries.
  - At install it seeds `/etc/opkg.conf` from `opkg.gpkg.tmp`. That template adds the Gargoyle plugin feeds (with `%G` → Gargoyle package version and `%F` → profile substituted at build time) and a `plugin_root` destination, so plugins can be installed to `/plugin_root`, which can be symlinked to USB storage.
- **`haserl-i18n` (`package/haserl-i18n/`).** A fork of haserl 0.9.36 (the CGI shell-template interpreter every `.sh` page runs under). It implements the `<%~ Key %>` translation tags at runtime: `h_translate.c` reads `gargoyle.global.language`/`fallback_lang`/`web_root` from UCI (linking `libuci` + `libericstools`) and loads the per-page translation files from `/www/i18n/<lang>/`. It's also installed as `/usr/bin/i18n`, which shell scripts use to translate single strings.
  - The core `gargoyle` package depends on it.
  - `i18n-scripts/finalize_translation.py` rewrites the target `.config` to choose between it and stock `haserl` (`package/haserl/`, also vendored). An `internationalize` build uses `haserl-i18n`. A `localize` build bakes translations into the pages and switches to stock `haserl`.
  - `haserl-i18n` builds from its own copy of the source in `src/`, so a `haserl` version bump doesn't carry over to it. Port the changes into `src/` by hand.

## Code conventions

From `CONTRIBUTING.md` — these diverge from stock Linux kernel style and are enforced by convention, not tooling:

- Indent with tabs, one tab per level; align continuation/offset content with spaces, not additional tabs.
- Opening and closing curly braces each get their own line (not K&R), *except* a genuinely one-line if/loop body.
- Every if/loop gets braces, even single-statement ones — except the one-line exception above.
- Plugins are named `plugin-gargoyle-*`; a plugin providing a GUI for an upstream package should be a separate package from that upstream package (see `minidlna` for the pattern).

## Notes for build.sh internals

- `build.sh` and `rebuild.sh` duplicate a lot of shared logic (`set_constant_variables`, `create_gargoyle_banner`, `do_js_compress`, `do_css_compress`, etc.) — they are not sourced from a common file, so a fix in one often needs to be mirrored in the other.
- `dev-utils/prep.sh` and `dev-utils/retest.sh` are legacy (reference a `kamikaze-7.09` OpenWrt source tree and hardcoded test-router credentials) and are not part of the current build flow — don't treat them as current documentation of the build process.
