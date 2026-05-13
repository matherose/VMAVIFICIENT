# False Positives Log

This file documents false positives from static analysis tools (clang-tidy, scan-build).

## Guidelines

When suppressing a check, add a **NOLINT** comment inline:

```c
// NOLINT(<check-name>): <reason>
 fclose(f);  // suppress cert-err33-c: return value intentionally ignored
```

And also document it here with full context:

- File and line number
- Check name
- Reason why it's a false positive
- Whether it was reviewed and approved

## Existing False Positives

### cert-err33-c (clang-analyzer)

- **File:** unsigned
- **Line:** N/A
- **Check:** cert-err33-c
- **Reason:** fclose() return value intentionally ignored for read-only file descriptors
- **Status:** reviewed
- **Suppression:** NOLINT(cert-err33-c): fclose return value intentionally ignored — fd is read-only

### misc-no-recursion

- **File:** unsigned
- **Line:** N/A
- **Check:** misc-no-recursion
- **Reason:** false positive — loop with bounded iterations, not recursion
- **Status:** reviewed
- **Suppression:** NOLINT(misc-no-recursion): loop with bounded iterations, not recursion

### readability-function-size

- **File:** unsigned
- **Line:** N/A
- **Check:** readability-function-size
- **Reason:** false positive — function complexity is low despite line count
- **Status:** reviewed
- **Suppression:** NOLINT(readability-function-size): function complexity is low despite line count

## Adding a New False Positive

1. Add inline `// NOLINT(<check>): <reason>` comment
2. Document in this file with full context
3. Ensure the reason is specific and reviewable
4. Re-run static analysis to confirm no new findings

## Review Frequency

Review false positives:

- Weekly: Triage new suppressions
- Monthly: Audit existing suppressions for validity
- Quarterly: Remove obsolete suppressions
