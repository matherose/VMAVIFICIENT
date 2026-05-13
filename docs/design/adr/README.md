# Architecture Decision Records

This directory contains Architecture Decision Records (ADRs) for the project.

## Format

Each ADR follows the [ADR 0001](https://github.com/joelparkerhenderson/architecture_decision_record) format:

```markdown
# ADR-001: <Title>

**Date:** YYYY-MM-DD  
**Status:** proposed | approved | deprecated | superseded

## Context

What is the issue that we're facing? What are the driving forces behind this decision?

## Decision

What is the change that we're proposing and/or doing?

## Consequences

What becomes easier or more difficult to do because of this change? What are the risks?

## Alternatives Considered

What other approaches did we consider? Why did we choose this one?

## Related

- AGENTS.md (global)
- Project AGENTS.md (this project)
- Migration plan: docs/MIGRATION-PLAN.md
```

## Current ADRs

| ADR     | Title                                      | Date       | Status   |
| ------- | ------------------------------------------ | ---------- | -------- |
| ADR-001 | Project migration for AGENTS.md compliance | 2026-05-13 | approved |

## Commands

```bash
# List all ADRs
ls docs/design/adr/

# View a specific ADR
cat docs/design/adr/ADR-001-migration-plan.md
```
