# CLAUDE.md

Instructions for Claude Code in this repository. The shared agent guide and the project rules are imported below so there is one source of truth: update AGENTS.md or RULES.md, not copies here.

@AGENTS.md
@RULES.md

## Claude-specific notes

- Start design or architecture answers from `docs/rensga-engine/design-doc/rensga-engine-design-doc.md`, and cite roadmap phases as "<Roadmap> <number>" the way the doc does.
- The roadmap prompt pills are written for Claude. When adding or editing them, write a prompt you could answer well in a fresh chat with no other context.
- After editing roadmap data or the design doc, rebuild, then check the result with the temp-folder `diff` from AGENTS.md before reporting the change as done.
- Do builds and experiments in your scratchpad, not in `docs/`.
- When a change touches both the design doc and a roadmap, update both in the same task.
