# attic — quarantined legacy Java surface (not built)

`attic/APIClient.java` is the original zero-GC Java telemetry client
(`api.APIClient.sendTelemetry`), moved here quarantine-first so history
stays intact while nothing in the build references it: CMake compiles
only `src/**/*.c` (no Java toolchain, no glob over this directory).

Canonical C shims (null-safe, `false`/`NULL` degrade — Rule 24/35):

* `APIClient.sendTelemetry` → `src/api/client.c`
  (`APIClient_sendTelemetry`)
* `DiscordWebhook` (reported at `com/discord/DiscordWebhook.java` —
  never present in this repo; no such file was moved) →
  `src/com/discord/discord.c`

The attic original keeps its `@Draft` / `@Intention` annotations as the
legacy intent record. Do not resurrect Java here; extend the C ports.
