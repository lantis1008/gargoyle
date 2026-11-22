---
name: check-vendored-packages
description: After bumping the OpenWrt commit in build.sh, report the pin dates and propose which vendored packages in package/ need updating from the matching openwrt/packages release branch (e.g. openwrt-24.10, never master). Use when the user bumps OpenWrt, asks which vendored packages are out of date, or wants an upstream package sync review.
---

# Check vendored packages against upstream OpenWrt

Gargoyle does not use OpenWrt feeds. Packages from `openwrt/packages` (and a few
OpenWrt core packages) are copied into `package/` and override upstream when
the tree is built. This skill finds the ones that have fallen behind the
OpenWrt release branch that Gargoyle is pinned to.

## Hard rule

Only compare against the release branch named by `packages_branch` in
`build.sh` (for example `openwrt-24.10`). **Never propose versions from
`master` or another branch**, even when a fix exists there. If the user expects
a fix that is only on master, say plainly that the release branch doesn't have
it yet, and stop there.

## Steps

1. **Run the report** (it takes about 15s; the first run clones into `~/.cache/gargoyle-pkg-check/`):
   ```sh
   python3 .claude/skills/check-vendored-packages/check_vendored.py > "$SCRATCH/vendored-report.md"
   ```
   Options:
   - `--old-commit SHA` sets the previous pin. By default it is read from `build.sh` at git `HEAD`, so a bump that is still uncommitted is detected automatically. Pass this flag once the bump has been committed.
   - `--only a,b` limits the run to the named packages.
   - `--all` also lists up-to-date and Gargoyle-only packages.
   - `--no-fetch` skips refreshing the cached upstream clones.

2. **Report the pins:** the old and new OpenWrt commit, the date of each, `git describe`, the number of commits between them, and whether the new pin is at the branch tip.

3. **Classify each candidate.** The script groups packages as:
   - *update*: the upstream release branch has a newer version or release.
   - *review*: same version, but upstream has commits since Gargoyle last touched the package (Makefile or patch fixes).
   - *ahead*: Gargoyle is newer than upstream, usually on purpose (e.g. `openvpn-easy-rsa`). Keep it unless the user says otherwise.
   - *core overrides*: packages that shadow an OpenWrt core package (e.g. `libusb`). These are compared between the old and new pin.

4. **Check for Gargoyle customisations** before proposing each update. Gargoyle-only patches are listed under *Details*. Also diff the Makefile and files:
   ```sh
   C=~/.cache/gargoyle-pkg-check/openwrt-packages.git B=openwrt-24.10 P=net/openvpn N=openvpn
   git -C $C show $B:$P/Makefile | diff -u - package/$N/Makefile
   git -C $C ls-tree -r --name-only $B -- $P        # upstream file list
   git -C $C log --format='%h %cs %s' $B -- $P | head  # history
   ```
   To separate Gargoyle's own changes from version drift, find the upstream commit that matched Gargoyle's current `PKG_VERSION` and diff against that commit instead. Use `git log -S'PKG_VERSION:=<ver>' $B -- $P`. A patch listed as "only in Gargoyle" may just be an upstream patch that upstream has since dropped (miniupnpd's `200-*`/`300-*` were like this). Check the base commit before calling anything a customisation. Known customised packages, which need a merge rather than a copy (add to this list as you find more):
   - `openvpn`: renamed to `openvpn-gargoyle-*` / `OPENVPN_gargoyle_*`, `PROVIDES openvpn-gargoyle`; `gargoyle_append_params` in `openvpn.init`; `globals.autostart 0` in `openvpn.config`.
   - `miniupnpd`: `upnp_lease_file6` (IPv6 pinhole leases) in `miniupnpd.init` and `upnpd.config`.
   - `minidlna`: `601-ignore-directory.patch`, `minidlna-refresh.hotplug` (+ install lines), `friendly_name 'Gargoyle DLNA Server'`.
   - `msmtp`: `+libopenssl` / `--with-tls=openssl` instead of gnutls.
   - `ksmbd-tools`: heavily rewritten `ksmbd.init`, `ksmbd.config` and template (`samba`/`sambashare` sections, user lists, homes, charset).
   - `openvpn-easy-rsa` (Gargoyle ahead, mbedtls patch), `https-dns-proxy`, `ffmpeg` (`050-glibc.patch`), `haserl`.

5. **Propose, don't apply.** For each package, give: current → upstream version, the upstream commits in between, which Gargoyle patches or changes must be carried forward (and whether they still apply or have been merged upstream), and the risk (major/minor version bump, new dependencies via `DEPENDS`, changed config options). Rank by importance: security (curl, openvpn, c-ares, sqlite3, miniupnpd …) and build fixes first. Only edit `package/` if the user asks.

## Notes

- If a package fails to build, check whether the failing step is under `build_dir/hostpkg` or `build_dir/host`. Those steps use the build machine's own compiler (`staging_dir/host/bin/gcc` points to `/usr/bin/cc`), not OpenWrt's toolchain. An upstream version bump often won't fix that kind of failure. For example, GCC 15+ defaults to C23 and breaks old-style `f()` declarations, which the 24.10 branch didn't fix for libtirpc. The fix is usually `HOST_CFLAGS += -std=gnu17` in the package Makefile.

- Packages are matched by directory name. If a vendored package is ever renamed relative to upstream, add it to `ALIASES` in the script.
- `PKG_RELEASE:=$(AUTORELEASE)` can't be compared numerically. These packages fall back to the commit log.
- The packages branch isn't pinned in `build.sh` (only the core commit is), so the comparison uses the packages branch tip. That matches what a `custom` build clones.
