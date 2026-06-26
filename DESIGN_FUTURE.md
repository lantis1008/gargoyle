# Gargoyle — A Path Forward

> **About this document:** This architectural analysis and proposal was produced
> with the assistance of Claude (Anthropic AI), working from a deep read of the
> current Gargoyle source tree. It is shared as a conversation starter — not a
> plan we intend to implement unilaterally. We'd genuinely value your perspective
> on whether the diagnosis is right and whether any of this direction is worth
> pursuing.
>
> *— shane / ispyisail*

---

## What the code audit found

After reading through the full stack — haserl, `common.js` (3,813 lines),
`run_commands.sh`, `gargoyle_header_footer.c`, `gargoyle_session_validator.c`,
`gargoyle_firewall_util.sh` (3,000+ lines), UCIContainer, the plugin system —
one foundational decision turned out to drive almost every current limitation:

> **Shell commands are the API.**

The browser builds strings of `uci set` / shell commands in JavaScript, POSTs
them to `run_commands.sh`, which writes them to `/tmp/tmp.sh` and runs
`sh /tmp/tmp.sh`. Every downstream weakness flows from that single design choice:

| Symptom | Root cause |
|---|---|
| Shell injection surface in JS save paths | String-built commands, no validation |
| `/tmp/tmp.sh` race condition (two saves in flight) | No command queue |
| Web server must run as root | `sh /tmp/tmp.sh` needs system access |
| No input validation before execution | Commands are opaque strings |
| IP-bound sessions that break on mobile | No server-side session store |
| No CSRF protection | Session cookie reused as operation token |
| No real-time updates | No persistent connection to server |
| Untestable UI logic | JS constructs shell; can't unit-test that |

That design choice was **entirely reasonable in 2008** on a 32 MB MIPS device
where a shell interpreter was already present and a Go binary would have been
unthinkable. On a GL-iNet MT6000 with 1 GB RAM and a quad-core ARM Cortex-A53,
the constraints that justified it are gone.

---

## What is genuinely good

Before proposing changes, it's worth being clear about what works well and
should be preserved:

- **UCI as the canonical config store** — it is the OpenWrt contract; every tool
  understands it; it should stay
- **The plugin/nav registration system** — elegant; new pages register via UCI
  without touching core code
- **bwmon-gargoyle** — the kernel-level bandwidth tracking is solid; the C
  kernel side should not be touched
- **The feature set** — bandwidth monitoring, quotas, QoS, access restrictions,
  known devices. These differentiate Gargoyle from vanilla LuCI and represent
  years of accumulated knowledge about making them work on real hardware
- **procd init scripts** — correct OpenWrt convention; keep
- **dnsmasq `dhcpscript` lease hooks** — the `post_lease.sh` pattern is clean
- **nftables / fw4** — the firewall layer itself is fine; the shell script that
  *generates* the rules is the problem

---

## The proposed new stack

### Backend — Go API server (`gargoyle-api`)

A single statically-linked Go binary replaces `gargoyle_header_footer` (C),
`gargoyle_session_validator` (C), and `run_commands.sh`. It:

- Serves a JSON API: `GET/POST /api/config/{network,firewall,dhcp,qos,quotas}`
- Streams real-time data: `GET /api/events` (Server-Sent Events)
- Serves static frontend assets
- Registers with procd like any other daemon

**Why Go over the alternatives:**

| Option | Why not |
|---|---|
| Continue C | Memory safety risk; hard to extend; no standard HTTP library |
| Lua + rpcd | rpcd's JSON-RPC model is awkward for streaming; weak types |
| Python/Flask | Runtime too heavy; slow cold start |
| Node.js | Too heavy for embedded |
| Go | Single binary, ~8 MB, cross-compiles for ARM/MIPS/x86, strong stdlib |

### Privilege separation — `gargoyle-helper`

`gargoyle-api` runs as an unprivileged `gargoyle` user. A second small Go binary
(`gargoyle-helper`) runs as root and communicates over a Unix domain socket. It
only accepts a fixed typed whitelist of operations — never a shell command string:

```json
{ "op": "uci-set",          "pkg": "network", "section": "lan", "option": "ipaddr", "value": "192.168.1.1" }
{ "op": "restart-service",  "name": "dnsmasq" }
{ "op": "apply-nft-rules"  }
{ "op": "reboot"           }
```

No shell strings cross the IPC boundary. The injection surface disappears.

### Configuration layer — typed Go structs ↔ UCI

```go
type WANConfig struct {
    Proto    string `uci:"proto"  validate:"oneof=dhcp pppoe static"`
    Hostname string `uci:"hostname" validate:"omitempty,hostname"`
    VLANID   *int   `uci:"vid"    validate:"omitempty,min=1,max=4094"`
}
```

`go-playground/validator` enforces constraints before any UCI write. The server
diffs current vs desired state and can report what will change before applying.
No string-built commands, anywhere.

### Frontend — HTMX + Alpine.js + Svelte components

Rather than a full single-page app (React/Vue), which would require a complex
build pipeline and produce a large bundle, the proposal uses a layered approach:

| Layer | Library | Size | Purpose |
|---|---|---|---|
| Page navigation & forms | HTMX | 14 KB | Server returns HTML fragments; HTMX swaps them in |
| Reactive state | Alpine.js | 7 KB | Inline reactivity for toggles, modals, dropdowns |
| Complex widgets | Svelte (compiled) | ~30 KB total | Bandwidth charts, device tables, VLAN port diagram |
| CSS | Tailwind (purged) | ~8 KB | Replaces Bootstrap 3's current 88 KB |

HTMX's mental model — "the server returns HTML, the browser swaps it in" — is
close to what the current team already knows. It is not a framework that needs
learning; it is HTML with a few extra attributes.

### Real-time — Server-Sent Events

One persistent HTTP connection (`GET /api/events`) replaces all polling and
page-refresh patterns. The Go server bridges ubus events and bwmon counter polls
into an SSE stream:

```
data: {"type":"bandwidth","ts":1718000000,"devices":[{"ip":"192.168.1.5","rx":12340,"tx":4560}]}

data: {"type":"lease","action":"add","mac":"aa:bb:cc:dd:ee:ff","ip":"192.168.1.20","name":"laptop"}

data: {"type":"wan","state":"up","proto":"dhcp","ip":"203.0.113.5"}
```

The bandwidth graph updates live. The device table updates when a lease event
fires. WAN status reflects link changes within a second.

### Session security

| Problem | Current | Proposed |
|---|---|---|
| Session binding | `SHA256(IP + UA + expiry)` — breaks on mobile | JWT with server-side store, not IP-bound |
| CSRF | None — session cookie reused as operation token | `SameSite=Strict` + synchronizer token |
| Privilege | Web server runs as root | Unprivileged daemon + typed helper |
| Audit | None | Structured JSON log: who changed what, when |
| Transport | Optional HTTPS | acme.sh integration, HSTS |

### i18n

JSON files per language (`/usr/share/gargoyle/i18n/en.json`) replace the
current JS-file-per-language model injected by the C binary. The browser fetches
only the active language on load. Language switching requires no page rebuild.

---

## The transition — not a rewrite

A full rewrite from scratch is how open-source router projects die. You spend
two years recreating things that already work and never ship anything new.

The proposal is a **strangler fig**: the existing system keeps running while new
infrastructure grows around it. Pages only move when there is a specific reason
to move them.

### Phase 1 — Replace the dangerous layer (one focused PR)

Replace `run_commands.sh` + `/tmp/tmp.sh` with the typed Go helper.

The JavaScript still builds "commands" but they become JSON objects, not shell
strings. The helper validates and executes them. Every existing haserl page keeps
working unchanged. UCIContainer keeps working unchanged.

**What this delivers alone:**
- Shell injection surface eliminated
- `/tmp/tmp.sh` race condition eliminated
- Web server can drop root privilege
- Sessions can move to proper JWT without touching any page code

This is a single reviewable PR with no UI changes and high security value.

### Phase 2 — Add JSON API alongside haserl (no migration required)

Spin up `gargoyle-api` serving `/api/` routes. Haserl pages continue to work
unchanged. New pages and features are written against the API from the start.

Existing pages migrate only when there is a reason — a feature need, a bug, a
redesign. Most simple pages (Access, Identification, Time, Backup) may never
need to migrate and that is fine.

### Phase 3 — New UI only for features that need it

Some features specifically require capabilities haserl cannot provide:

- **VLAN manager** — port diagram, drag-and-drop VLAN assignment per physical port
- **Real-time bandwidth** — live chart streaming from SSE, not a table you refresh
- **Connected devices** — updates on join/leave without page reload
- **QoS class editor** — visual drag-and-drop class priority

These get Svelte components served by the new API. Everything else stays as-is.

---

## Summary

| | Keep | Replace |
|---|---|---|
| Config store | UCI | — |
| Plugin nav system | UCI registration model | Replace UCI config with JSON manifest |
| Bandwidth tracking | bwmon-gargoyle kernel module | — |
| Feature set | All of it | — |
| Init system | procd | — |
| Firewall | nftables / fw4 | `gargoyle_firewall_util.sh` → Go |
| Web server | uhttpd (or replace with Go's built-in) | — |
| Template engine | — | haserl → Go `text/template` + HTMX |
| Command execution | — | `run_commands.sh` → typed Go helper |
| Client config model | — | UCIContainer (JS) → Go structs server-side |
| Header/footer binary | — | `gargoyle_header_footer` (C) → Go |
| Session validator | — | `gargoyle_session_validator` (C) → Go JWT |
| CSS | — | Bootstrap 3 → Tailwind (purged) |

The hardest parts are not the framework choices — they are the UCI marshaling
layer (Go structs ↔ UCI's flat key-value model), the nftables rule generation
(currently 3,000 lines of shell that knows the exact chain structure), and the
bwmon SSE bridge. These are each a substantial but bounded engineering task.

---

## Questions for the maintainer

1. Does the core diagnosis — "shell commands as the API is the root problem" —
   match your experience, or do you see the biggest pain points differently?

2. Is flash/RAM still a real constraint on target devices, or has the hardware
   moved on enough that an ~8 MB Go binary is acceptable?

3. Is there prior art in the OpenWrt ecosystem worth studying — any project that
   has solved the "typed API over UCI" problem well?

4. Even if the full vision is not viable, would you accept a Phase 1 PR that
   replaces `run_commands.sh` with a typed helper while leaving every existing
   page untouched?

5. Is a DESIGN doc like this an appropriate artifact to keep in the repo, or
   would you prefer this conversation happen elsewhere (wiki, forum thread)?
