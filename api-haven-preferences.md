# api-haven — Repo-Local Living Preferences
> Repo-local preferences governed by the Living Documentation Law.
> Universal Supreme Constitution: preferences.md (vexspoke).

## 0. Constitution Link (supreme)
- [preferences.md](https://github.com/vexgraph-ecosystem/vexspoke/blob/main/preferences.md) (canonical, vexspoke) — accessible locally at ../../preferences.md
- All universal laws in `../../../preferences.md` are mandatory and binding across the ecosystem.
- This document codifies **exclusive** preferences that apply uniquely to `api-haven` (R3 API Connector).

## 1. Repo-Local Law Index (Binding Matrix)

Universal laws are inherited from the canonical `../../../preferences.md` Index; this table indexes the additional laws specific to this repository.

| Law Title | Scope | Enforcement |
| :--- | :--- | :--- |
| **Asset Sourcing Law — Legal-Sense, First-Class In-App Marketplace Policy** | R3 API Connector | Mandatory for `api-haven` |
| **Quarantined Attic Isolation Law** | R3 API Connector | Mandatory for `api-haven` |
| **Zero-Allocation Telemetry Law** | R3 API Connector | Mandatory for `api-haven` |

## 2. Exclusive Repo-Local Laws (FULL PROSE RESTATEMENT)

### Asset Sourcing Law — Legal-Sense, First-Class In-App Marketplace Policy

#### Definition:
Every external asset source (images, 3D, textures, audio) is a row in api-haven's AssetProvider descriptor registry. Only blessed public APIs and direct-download URLs offered by the source are wired. Sources without a public search API are catalog-only rows with curated static manifests and hand-verified URLs, or excluded. Interface scraping — parsing another service's HTML/JSON to fake a search API, or bypassing auth — is a defect, always.

#### The Why:
Legal exposure, broken trust, and brittle integrations come from scraping. A first-class in-app marketplace must be built on explicit contracts, normalized shapes, and license-aware flows — not on reverse-engineered endpoints that vanish or change without notice.

#### The Rule:
- **Catalog, not scraping.** Blessed providers: Unsplash, Pexels, Pixabay, Openverse, Wikimedia Commons, Sketchfab, Freesound, Poly Haven, AmbientCG, OpenGameArt, Google Custom Search JSON API. Each is a row in the AssetProvider registry with its public search API. Sources without a public search API (Pinterest, raw Google Images, Kenney, Quaternius, itch.io packs) are catalog-only rows with curated static manifests and hand-verified URLs, or excluded. Interface scraping is a defect.
- **One normalized contract.** Every search result is an AssetRow (provider slug, id, title, author, license family, preview/download URLs, attribution, dimensions/duration, size). The UI never sees provider-specific shapes.
- **License is a field, not a footnote.** Every row carries a license family; attribution is rendered before import; project export fails closed on UNKNOWN license.
- **Downloads land in the cache.** AssetBroker_download streams into VexHome_cache(<subsystem>) with bounded timeouts (the Bounded Wait Law); cache files are shim state tracked and closed before Memory_freeAll (the Vertical Integration Law (Teardown)). No exec, no writes outside the cache.
- **The UI seam is fn-pointers.** darling hosts AssetBrowser and never includes api-haven (the Vertical Integration Law); the R5 app binds an AssetSource fn-pointer table (opaque handle + callbacks — the Conflict Triage Law canonical move).
- **MCP surface.** asset_source_lookup / asset_search / asset_download hosted by McpServer; writes cache-confined, timeouts bounded, no exec.
- **Credentials.** API keys via vexspoke Keychain or ASSET_KEY_<SLUG> env rendered by ApiAuth; never stored in the arena, prefs, or repo.

---

### Quarantined Attic Isolation Law

#### Definition:
All legacy Java source code and off-heap prototypes are permanently quarantined under `attic/`. Production C23 builds must never include, link, or reference any files in `attic/`.

#### The Why:
Historical code preserves context but must never pollute the pure C23 compilation unit or confuse static analysis tools.

#### The Rule:
1. **No Attic Includes:** Code in `src/api` or `src/com` must never include headers or references from `attic/`.
2. **Build Isolation:** CMake configurations ignore `attic/` completely.

---

### Zero-Allocation Telemetry Law

#### Definition:
Telemetry feeds, Server-Sent Events (SSE), and Model Context Protocol (MCP) message serialization operate strictly on pre-allocated circular buffers and arena memory with zero steady-state heap allocations.

#### The Why:
Network telemetry streams must not induce memory allocator churn or thread stalling in high-throughput API pipelines.

#### The Rule:
1. **Ring Buffering:** Inbound and outbound packets serialize into pre-allocated memory slots.
2. **Bounded Capacity:** Overflow triggers structured backpressure or packet drops rather than emergency reallocations.

---

## 3. Repo-Local Extensions (managed, per the Conflict Triage Law)

;;INTENTION("R3 API Surface: POSIX/macOS native connectors; quarantined attic; zero runtime allocation in telemetry loops.")

---

## 4. Readiness Cross-Reference (Living Documentation Law)

- Feature readiness matrix tracked in [`../../_repositories/.ecosystem/api-haven.md`](../../_repositories/.ecosystem/api-haven.md) (rendered as `[[api-haven]]` wiki page).
