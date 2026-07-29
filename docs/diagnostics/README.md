# Diagnostic Evidence

This directory retains diagnostic evidence that is useful after transient
build output has been cleaned.

- `programming/` contains opt-in GUI session logs and logic-analyzer captures
  selected for investigation.
- `firmware_build/` contains build baselines selected for comparison.

Routine generated logs are not source files. Archive only evidence that is
referenced by an active investigation or verification record. Remove
workstation paths, operator/customer identifiers, and unrelated target
firmware before committing diagnostic evidence.
