#!/usr/bin/env python3
"""Compare Gargoyle's vendored packages (package/*) against upstream OpenWrt.

Reads the OpenWrt pin from build.sh, works out the pin it replaced (from git
HEAD's build.sh, or --old-commit), and for every package/<name>/Makefile looks
for a same-named package in:
  * openwrt/packages  (ONLY the release branch named by packages_branch in
                       build.sh, e.g. openwrt-24.10 -- never master)
  * the OpenWrt core tree itself (package/**) at the new pin

It prints a Markdown report to stdout. It never modifies the Gargoyle tree.

Usage: check_vendored.py [--repo DIR] [--old-commit SHA] [--cache DIR]
                         [--no-fetch] [--only name,name] [--all]
"""
import argparse
import os
import re
import subprocess
import sys
from datetime import datetime

# Gargoyle package dir name -> upstream package dir basename, where they differ.
# Gargoyle package dir -> upstream dir basename, for renamed forks that the
# automatic "strip gargoyle-/-gargoyle" rule below doesn't catch.
ALIASES = {
	"haserl-i18n": "haserl",
}


def upstream_name(n, *indexes):
	"""Name to look up upstream: explicit alias, exact name, or name minus gargoyle- prefix/-gargoyle suffix."""
	if n in ALIASES:
		return ALIASES[n]
	if any(n in idx for idx in indexes):
		return n
	for cand in (n[len("gargoyle-"):] if n.startswith("gargoyle-") else None,
	             n[: -len("-gargoyle")] if n.endswith("-gargoyle") else None):
		if cand and any(cand in idx for idx in indexes):
			return cand
	return n

VER_KEYS = ("PKG_VERSION", "PKG_RELEASE", "PKG_SOURCE_VERSION", "PKG_SOURCE_DATE", "PKG_HASH", "PKG_MIRROR_HASH")


def run(cmd, cwd=None, check=True):
	r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
	if check and r.returncode != 0:
		raise RuntimeError("%s failed: %s" % (" ".join(cmd), r.stderr.strip()))
	return r.stdout


def git(repo, *args, check=True):
	return run(["git", "-C", repo] + list(args), check=check)


def parse_makefile(text):
	"""Very small make-variable evaluator: enough for PKG_* assignments."""
	vars_ = {}
	for line in text.splitlines():
		m = re.match(r"^\s*([A-Z0-9_]+)\s*(:=|\?=|=|\+=)\s*(.*?)\s*$", line)
		if not m:
			continue
		k, op, v = m.groups()
		if op == "?=" and k in vars_:
			continue
		if op == "+=":
			vars_[k] = (vars_.get(k, "") + " " + v).strip()
		else:
			vars_[k] = v

	def expand(v, depth=0):
		if depth > 10:
			return v
		return re.sub(r"\$\(([A-Z0-9_]+)\)", lambda m: expand(vars_.get(m.group(1), m.group(0)), depth + 1), v)

	return {k: expand(vars_[k]) for k in VER_KEYS if k in vars_}


def ver_str(d):
	if d is None:
		return "—"
	v = d.get("PKG_VERSION") or d.get("PKG_SOURCE_DATE") or "?"
	if d.get("PKG_SOURCE_VERSION"):
		v += "~" + d["PKG_SOURCE_VERSION"][:8]
	return "%s-r%s" % (v, d.get("PKG_RELEASE", "?"))


def same_source(a, b):
	if a is None or b is None:
		return False
	for k in ("PKG_VERSION", "PKG_SOURCE_VERSION", "PKG_SOURCE_DATE"):
		if a.get(k) != b.get(k):
			return False
	return True


def ver_tuple(v):
	return tuple(int(x) if x.isdigit() else x for x in re.split(r"[.\-_~]", v or "") if x)


def compare(a, b):
	"""-1 gargoyle a older than upstream b, 0 same, 1 gargoyle ahead (best effort)."""
	if a is None or b is None:
		return 0
	if same_source(a, b):
		try:
			ra, rb = int(a.get("PKG_RELEASE", 0)), int(b.get("PKG_RELEASE", 0))
		except ValueError:
			return 0	# e.g. $(AUTORELEASE): can't tell, rely on the commit log instead
		return (ra > rb) - (ra < rb)
	if a.get("PKG_VERSION") == b.get("PKG_VERSION"):
		return -1	# same version, different source commit/date: treat as upstream newer
	try:
		va = ver_tuple(a.get("PKG_VERSION") or a.get("PKG_SOURCE_DATE"))
		vb = ver_tuple(b.get("PKG_VERSION") or b.get("PKG_SOURCE_DATE"))
		return (va > vb) - (va < vb)
	except TypeError:
		return -1


def newer(a, b):
	return compare(a, b) < 0


def tree_index(repo, ref, prefix=""):
	"""basename -> path for every directory containing a Makefile."""
	out = git(repo, "ls-tree", "-r", "--name-only", ref, *( [prefix] if prefix else [] ))
	idx = {}
	for p in out.splitlines():
		if p.endswith("/Makefile"):
			d = p[: -len("/Makefile")]
			idx.setdefault(os.path.basename(d), d)
	return idx


def show(repo, ref, path):
	r = subprocess.run(["git", "-C", repo, "show", "%s:%s" % (ref, path)], capture_output=True, text=True)
	return r.stdout if r.returncode == 0 else None


def identical(repo, ref, d, local_dir):
	"""True if local_dir has exactly the same files/contents as ref:d (modes ignored)."""
	up = {}
	for line in git(repo, "ls-tree", "-r", ref, "--", d + "/").splitlines():
		meta, p = line.split("\t", 1)
		up[os.path.relpath(p, d)] = meta.split()[2]
	local = []
	for root, _, files in os.walk(local_dir):
		for f in files:
			local.append(os.path.relpath(os.path.join(root, f), local_dir))
	if sorted(local) != sorted(up):
		return False
	paths = [os.path.join(local_dir, p) for p in sorted(local)]
	hashes = subprocess.run(["git", "hash-object", "--no-filters", "--stdin-paths"],
	                        input="\n".join(paths) + "\n", capture_output=True, text=True).stdout.split()
	return hashes == [up[p] for p in sorted(local)]


def patches_at(repo, ref, d):
	out = git(repo, "ls-tree", "--name-only", "%s:%s/patches" % (ref, d), check=False)
	return set(out.split())


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--repo", default=os.getcwd())
	ap.add_argument("--old-commit", help="previous OpenWrt pin (default: build.sh at git HEAD)")
	ap.add_argument("--cache", default=os.path.expanduser("~/.cache/gargoyle-pkg-check"))
	ap.add_argument("--no-fetch", action="store_true")
	ap.add_argument("--only", help="comma separated package names")
	ap.add_argument("--all", action="store_true", help="also list up-to-date / Gargoyle-only packages")
	a = ap.parse_args()

	repo = os.path.abspath(a.repo)
	build_sh = open(os.path.join(repo, "build.sh")).read()

	def bs_var(text, name):
		m = re.search(r'^\s*%s="([^"]*)"' % name, text, re.M)
		return m.group(1) if m else None

	new_commit = bs_var(build_sh, "openwrt_commit")
	branch_id = bs_var(build_sh, "branch_id")
	pkg_branch = bs_var(build_sh, "packages_branch") or branch_id
	old_commit = a.old_commit
	if not old_commit:
		old_commit = bs_var(git(repo, "show", "HEAD:build.sh"), "openwrt_commit")

	os.makedirs(a.cache, exist_ok=True)
	core = os.path.join(a.cache, "openwrt.git")
	pkgs = os.path.join(a.cache, "openwrt-packages.git")
	if not os.path.isdir(core):
		run(["git", "clone", "-q", "--bare", "--filter=blob:none", "https://github.com/openwrt/openwrt.git", core])
	if not os.path.isdir(pkgs):
		run(["git", "clone", "-q", "--bare", "https://github.com/openwrt/packages.git", pkgs])
	if not a.no_fetch:
		git(core, "fetch", "-q", "origin", "+refs/heads/%s:refs/heads/%s" % (branch_id, branch_id))
		git(pkgs, "fetch", "-q", "origin", "+refs/heads/%s:refs/heads/%s" % (pkg_branch, pkg_branch))

	def cinfo(sha):
		if not sha:
			return None
		out = git(core, "log", "-1", "--format=%H|%cI|%s", sha, check=False).strip()
		if not out:
			return None
		h, d, s = out.split("|", 2)
		desc = git(core, "describe", "--tags", sha, check=False).strip()
		return {"sha": h, "date": d, "subject": s, "describe": desc}

	new_i = cinfo(new_commit)
	old_i = cinfo(old_commit) if old_commit != new_commit else None
	core_tip = git(core, "rev-parse", branch_id).strip()
	pkg_tip = git(pkgs, "log", "-1", "--format=%h %cI", pkg_branch).strip()
	since = old_i["date"] if old_i else None

	print("# Vendored package check\n")
	print("Generated %s\n" % datetime.now().strftime("%Y-%m-%d %H:%M"))
	print("| | commit | date | describe |\n|---|---|---|---|")
	if old_i:
		print("| Old OpenWrt pin | `%s` | %s | %s |" % (old_i["sha"][:10], old_i["date"], old_i["describe"]))
	if new_i:
		print("| New OpenWrt pin | `%s` | %s | %s |" % (new_i["sha"][:10], new_i["date"], new_i["describe"]))
	else:
		print("| New OpenWrt pin | `%s` | not found in %s | |" % (new_commit, branch_id))
	print("| OpenWrt `%s` tip | `%s` | | %s |" % (branch_id, core_tip[:10], "pin == tip" if new_commit and core_tip.startswith(new_commit) else "**pin is behind tip**"))
	print("| packages `%s` tip | `%s` | %s | |" % (pkg_branch, *pkg_tip.split()))
	if old_i and new_i:
		print("\n%s core commits between pins.\n" % git(core, "rev-list", "--count", "%s..%s" % (old_commit, new_commit)).strip())

	pidx = tree_index(pkgs, pkg_branch)
	cidx = tree_index(core, new_commit, "package") if new_i else {}

	names = sorted(d for d in os.listdir(os.path.join(repo, "package")) if os.path.isfile(os.path.join(repo, "package", d, "Makefile")))
	if a.only:
		want = set(a.only.split(","))
		names = [n for n in names if n in want]

	rows = {"update": [], "review": [], "ahead": [], "core": [], "ok": [], "none": []}
	details = []
	for n in names:
		un = upstream_name(n, pidx, cidx)
		gdir = os.path.join(repo, "package", n)
		g = parse_makefile(open(os.path.join(gdir, "Makefile")).read())
		g_last = git(repo, "log", "-1", "--format=%cI", "--", "package/" + n).strip() or None
		g_patches = set(os.listdir(os.path.join(gdir, "patches"))) if os.path.isdir(os.path.join(gdir, "patches")) else set()

		if un in cidx and un not in pidx:
			path = cidx[un]
			u = parse_makefile(show(core, new_commit, path + "/Makefile") or "")
			uo = parse_makefile(show(core, old_commit, path + "/Makefile") or "") if old_i else None
			log = git(core, "log", "--format=%h %cs %s", "%s..%s" % (old_commit, new_commit), "--", path).strip() if old_i else ""
			rows["core"].append((n, path, ver_str(g), ver_str(uo), ver_str(u), len(log.splitlines()) if log else 0))
			if log or newer(g, u):
				details.append((n, "openwrt core " + path, g, u, log, g_patches, patches_at(core, new_commit, path)))
			continue

		if un not in pidx:
			rows["none"].append(n)
			continue

		path = pidx[un]
		u = parse_makefile(show(pkgs, pkg_branch, path + "/Makefile") or "")
		# commits since the later of: previous OpenWrt pin date, last Gargoyle change to this package
		ref_date = max(x for x in (g_last, since) if x) if (g_last or since) else None
		log = git(pkgs, "log", "--format=%h %cs %s", "--since=" + ref_date, pkg_branch, "--", path).strip() if ref_date else ""

		c = compare(g, u)
		if identical(pkgs, pkg_branch, path, gdir):
			log = ""	# exact copy of upstream tip (possibly not yet committed)
			cat = "ok"
		elif c < 0:
			cat = "update"
		elif c > 0:
			cat = "ahead"
		elif log:
			cat = "review"
		else:
			cat = "ok"
		rows[cat].append((n, path, ver_str(g), ver_str(u), g_last[:10] if g_last else "—", len(log.splitlines()) if log else 0))
		if cat != "ok":
			details.append((n, "packages " + path, g, u, log, g_patches, patches_at(pkgs, pkg_branch, path)))

	def table(title, key, hdr):
		r = rows[key]
		if not r:
			return
		print("\n## %s (%d)\n" % (title, len(r)))
		print("| " + " | ".join(hdr) + " |\n|" + "---|" * len(hdr))
		for row in r:
			print("| " + " | ".join("`%s`" % c if i == 0 else str(c) for i, c in enumerate(row)) + " |")

	H = ["package", "upstream path", "gargoyle", pkg_branch, "gargoyle last touched", "upstream commits since"]
	table("Newer on %s: update candidates" % pkg_branch, "update", H)
	table("Same version, but upstream %s commits since last sync: review" % pkg_branch, "review", H)
	table("Gargoyle ahead of %s (deliberate? keep)" % pkg_branch, "ahead", H)
	table("Overrides of OpenWrt core packages", "core", ["package", "core path", "gargoyle", "old pin", "new pin", "core commits between pins"])
	if a.all:
		table("Up to date", "ok", H)
		if rows["none"]:
			print("\n## Gargoyle-only / no upstream match (%d)\n\n%s" % (len(rows["none"]), ", ".join("`%s`" % x for x in rows["none"])))
	else:
		print("\n%d up to date, %d Gargoyle-only/no upstream match (use --all to list).\n" % (len(rows["ok"]), len(rows["none"])))

	if details:
		print("\n## Details\n")
	for n, where, g, u, log, gp, up in details:
		print("### `%s` (%s)\n" % (n, where))
		print("- gargoyle: %s / upstream: %s" % (ver_str(g), ver_str(u)))
		if gp != up and (gp or up):
			if up - gp:
				print("- patches only upstream: " + ", ".join(sorted(up - gp)))
			if gp - up:
				print("- patches only in Gargoyle (customisations or dropped upstream): " + ", ".join(sorted(gp - up)))
		if log:
			print("- upstream commits:\n" + "\n".join("  - " + l for l in log.splitlines()[:15]))
		print()


if __name__ == "__main__":
	sys.exit(main())
