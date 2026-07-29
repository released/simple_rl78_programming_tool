# ADR-0005: Production Package Security

- Status: Proposed / blocked
- Date: 2026-07-11

## Context

Production offline packages and programmer updates need authenticity, optional confidentiality, anti-rollback and clear key ownership. M032SE3AE has limited Flash/SRAM and 512-byte SPROM.

## Proposed Decision

OIS remains algorithm-agile. Development may explicitly allow unsigned packages; production requires an approved asymmetric signature, key ID, provisioning, revocation and rollback policy. ECDSA P-256/SHA-256 is reserved as the baseline candidate, subject to footprint/performance and security review. Private signing keys remain off-station/off-device.

## Information Required

Threat model, customer requirements, build/release signer, device trust anchors, key rotation/revocation, secure provisioning, rollback/recovery authority, crypto library/secure element and export/compliance constraints.

## Acceptance Evidence

Security architecture/threat review, known-answer and negative tests, measured footprint/time, provision/revoke/recover procedures, signed golden packages and key-custody audit.

