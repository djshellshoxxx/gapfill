# Architecture decision records (ADRs)

Record durable technical choices that affect module boundaries, public plugin behavior, saved state, real-time processing, dependencies, supported formats, or release validation. Do not create an ADR for routine implementation details or a choice already fixed by an approved spec.

## When to write one

Create an ADR before implementation when a decision:

- Adds or replaces a runtime/build dependency.
- Changes DSP signal flow, thread ownership, module boundaries, or plugin host contracts.
- Changes parameter IDs, automation, latency/PDC, state/preset compatibility, or plugin formats.
- Has multiple plausible options with materially different testing, maintenance, licensing, or CPU/memory cost.

If the decision is small and reversible, document it in the feature spec or PR instead.

## Workflow

1. Copy the template below to `docs/adr/NNNN-short-title.md`; use the next unused four-digit number.
2. Set status to `Proposed` and link the ADR from the relevant spec/PR.
3. Describe alternatives and the reason for the chosen option, including real-time and cross-platform impact.
4. Mark `Accepted` only when the responsible maintainer approves the decision. Code must not treat a proposed ADR as authorization.
5. If superseded, keep the record and link the replacement; never rewrite history to make the new decision appear original.

## Template

```markdown
# ADR NNNN: Short decision title

- Status: Proposed | Accepted | Superseded by ADR NNNN
- Date: YYYY-MM-DD
- Owners: names or roles
- Related spec/issue/PR: links

## Context
What problem or constraint requires a decision? Include the affected users and current behavior.

## Decision
State the chosen approach in one or two testable sentences.

## Alternatives considered
| Option | Benefits | Costs/risks | Why selected or rejected |
|---|---|---|---|

## Consequences
- Positive:
- Negative/tradeoffs:
- Compatibility/migration:
- Real-time/threading:
- CPU/memory/latency:
- Licensing/security/privacy:

## Verification
List the tests, builds, host checks, and measurements required to show the decision works.

## Rollback or revisit trigger
State what evidence would justify changing this decision.
```

## Register

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-no-free-pro-split-in-1x.md) | No Free/Pro edition split in 1.x (F-P01) | Proposed |
| [0002](0002-aax-auv3-ios-out-of-scope-in-1x.md) | AAX, AUv3 and iOS out of scope in 1.x (F-P03) | Proposed |
