---
name: 📋 RFC / Design Proposal
about: Propose a significant design or architecture change for BUFUS
title: "[RFC] "
labels: rfc
assignees: ''
---

<!--
Use this template for substantial design changes — new features that affect
the core architecture, changes to the disk writing pipeline, new filesystem
support, major UI rework, etc.

For smaller changes, use a regular issue or pull request instead.
-->

## Motivation

<!--
Why is this change needed? What problem does it solve?
What limitations in the current design does this address?
-->


## Proposal

<!--
Describe the design in detail. Include:
- High-level architecture changes
- New components or modules
- Changes to existing APIs or data structures
- User-facing changes (if any)
- How this interacts with existing disk I/O and verification logic
-->

### Technical Details

<!--
- Win32 API calls involved
- Memory layout or data structure changes
- Filesystem or partition table implications
- Performance considerations (especially for large writes)
-->


### Safety & Correctness

<!--
BUFUS operates on raw physical devices. Describe:
- How this change affects data safety
- Any new destructive operation risks
- How verification/validation is affected
- Whether admin privilege requirements change
-->


### Alternatives Considered

<!--
List alternative approaches you evaluated and why this one is preferred.
-->


### Drawbacks & Risks

<!--
Honest assessment of trade-offs, complexity cost, and potential issues.
-->


### Implementation Plan

<!--
Rough breakdown of tasks or milestones. Optional but appreciated.
List which files/modules would need changes.
-->