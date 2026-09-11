# Contributions & Engineering Manifesto (api-haven)

This project is a strictly solo development process conducted in tight pair-programming partnership with an AI coding assistant.

It serves as an architectural manifesto for a **zero-allocation C23 systems library**, speaking raw memory layouts and native POSIX/macOS primitives through `vexspoke`.

> Legacy note: the original Java surface (`api.APIClient`, zero-GC
> off-heap telemetry) is quarantined under `attic/` and is not built.
> The canonical ports live in `src/api/` and `src/com/` — extend those,
> never the attic.

---

## 1. The AI-First Architecture Manifesto & Boilerplate Defense

This codebase strictly enforces the verbose, explicit boilerplate required across the `vexgraph` ecosystem:
- Single Class Per File (the Java Law: one public `typedef struct` per `.h`/`.c` pair).
- Absolute zero allocation in tick, layout, or event loops (no hidden temporaries).
- Complete, symmetric getters and setters for every state-bearing field.
- Explicit pointer discipline (`(*ptr).field`, `(T*) var` casts, `T *name` decls) rather than wrapper types.

### Why the Boilerplate Exists
This boilerplate is **not** an accident, nor is it a misunderstanding of idiomatic C. It is an intentional, machine-verifiable scaffold built specifically for **AI-Human Pair Systems Programming**:
1. **Machine Comprehension**: Explicit pointers and manual field mapping allow an AI coding agent to verify alignment and struct layout with zero ambiguity.
2. **AI-Maintained Rigor**: The AI agent writes and maintains repetitive accessor boilerplate, eliminating human typing toil while ensuring zero steady-state allocation.

---

## 2. Sanity Warning for External Contributors

> [!WARNING]
> **SANITY NOTICE FOR EXTERNAL CONTRIBUTORS**
> This repository is not designed for traditional C conveniences, casual hacking, or stylistic shortcuts. It is an unapologetic, machine-verifiable manifesto of AI-augmented zero-allocation systems architecture.
>
> **If you do not approve of this architecture or cannot find peace with this philosophy, consider leaving this repository for your own sanity.**
>
> We do not accept Pull Requests, issues, or unsolicited stylistic refactors attempting to re-introduce arrow sugar, collapse classes into shared files, bypass zero-allocation invariants, or resurrect the quarantined Java surface. Upstream is maintained exclusively by the author and the AI agent.

---

## 3. Supreme Living Document: `preferences.md`

All architectural rules and style invariants are governed by the central constitution:

- **[preferences.md](https://github.com/vexgraph-dev/vexspoke/blob/main/preferences.md)** (tracked in `vexspoke`, accessible locally at `../../preferences.md`)

Whenever preferences or conventions evolve, `preferences.md` is updated and committed locally in the same cycle (Zero Drift Law).
