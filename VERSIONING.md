# Versioning Policy

birun follows **Semantic Versioning 2.0.0**.

## Version axes

| Axis | Macro | Meaning |
|------|-------|---------|
| CLI / release | `BIRUN_VERSION_MAJOR.MINOR.PATCH` | User-facing `birun --version` |
| C++ header API | `BIRUN_API_VERSION` | Source compatibility of `*.hpp` |
| Cache format | `BIRUN_CACHE_FORMAT` | On-disk cache layout + hash inputs |

These can move independently. E.g. adding a new builtin bumps the
CLI MINOR but not the C++ API version.

## Compatibility promises

### CLI (birun --version)

- **MAJOR**: breaking change to `birun.bi` syntax, CLI flags, or exit-code semantics.
- **MINOR**: new builtins, new flags, new subcommands. Old configs keep working.
- **PATCH**: bug fixes only. No new features, no behavior changes.

### C++ header API

- **MAJOR**: rename / remove a public symbol, change a struct layout, change a function signature.
- **MINOR**: new inline helpers, new struct fields appended at the end.
- **PATCH**: inline-only bug fixes.

Any header change is documented in `CHANGELOG.md` under `### API`.

### Cache format

Bump `BIRUN_CACHE_FORMAT` (and `BIRUN_CACHE_MAGIC`) whenever:

- the file layout changes, OR
- any field that feeds into `cache::hashTask()` changes, OR
- the hash algorithm changes.

On mismatch, the old cache is silently discarded. Users never need to
run `--clean` manually after an upgrade.

## Deprecation process

1. Deprecated feature emits a warning (`warn(...)`).
2. Warning stays for one MINOR release.
3. Feature is removed in the next MAJOR release.

## What 1.0.0 guarantees

- No breaking changes to the `birun.bi` config syntax without a MAJOR bump.
- No breaking changes to exit codes or flag names without a MAJOR bump.
- No silent cache corruption across upgrades — either the cache is valid
  or it is discarded.