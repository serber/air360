# Firmware Doc Template

Use this header shape for implementation-oriented documents in `docs/firmware/`.

```md
# Document Title

## Status

Implemented. Keep this document aligned with the current `firmware/` tree.

## Scope

Explain exactly which subsystem, runtime path, or user workflow the document covers.

## Source of truth in code

- `firmware/path/to/file.cpp`
- `firmware/path/to/file.hpp`

## Read next

- `related-doc-a.md`
- `related-doc-b.md`
```

## Notes

- `Status` should describe whether the document explains implemented behavior, not planned work.
- `Scope` should say what is intentionally out of scope when that prevents confusion.
- `Source of truth in code` should point to the smallest real set of files that confirm the documented behavior.
- `Read next` should route the reader to the next most likely document for the same task.
- ADRs keep their existing ADR structure and do not need this exact header.
