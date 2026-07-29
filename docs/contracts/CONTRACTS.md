# Common Contract Registry

| Field | Value |
| --- | --- |
| Document ID | CCR-RPP-001 |
| Version | 1.0-draft |
| Scope | Shared PC, SDK, wire, firmware, OIS, plug-in and test contracts |

## 1. Authority Map

This registry owns shared primitives, capability/error/log/config/version compatibility rules. It deliberately does not duplicate these authoritative registries:

| Contract | Authority |
| --- | --- |
| PLP report, flags, commands, programmer states and command payloads | `docs/protocol/PPS.md` |
| OIP header, manifest, section types, algorithms and package states | `docs/offline/OIS.md` |
| Public host SDK operation/ABI contract | `docs/sdk/PSDK.md` |
| Device driver operations and firmware service boundaries | `docs/firmware/FSDS.md` |

Future machine-readable schemas in `shared/schemas/` are generated from these normative sources and must identify source document/version. Generated artifacts are never edited as a second truth source.

## 2. Shared Primitive Contracts

| Primitive | Definition |
| --- | --- |
| Byte order | Little-endian unless an exact field (UUID/hash) says raw/network order |
| UUID | 16 RFC 4122 bytes; textual form lower-case canonical for files/UI |
| SHA-256 | 32 raw bytes; textual lower-case hexadecimal when required |
| CRC16 | CRC-16/CCITT-FALSE per PPS |
| CRC32 | CRC-32/ISO-HDLC per OIS |
| Timestamp | Unix epoch milliseconds UTC `u64`; zero means unavailable, not 1970 event |
| Monotonic duration | Milliseconds from wrap-safe monotonic clock |
| UTF-8 | Valid UTF-8 byte span, explicit size, no terminator in binary contracts |
| Boolean | `u8`, only `0` or `1`; other values invalid |
| Percentage | `u16` basis points `0..10000`; unknown carried by explicit validity flag |
| Address | Unsigned logical byte address plus explicit width/address-space ID |

## 3. Unified Error Code

### 3.1 Layout

Success is exactly `0x00000000`. Nonzero codes use:

| Bits | Field | Meaning |
| --- | --- | --- |
| 31..28 | Severity | `1` warning, `2` error, `3` fatal; `4..15` reserved |
| 27..20 | Category | Stable domain category |
| 19..12 | Component | Stable producer/owner ID within category |
| 11..0 | Reason | Stable reason ID; `0` is category-generic only when allocated |

Numeric severity is diagnostic; terminal success/failure follows operation state and zero/nonzero policy. Warnings do not masquerade as success when a contract requires explicit acknowledgment.

### 3.2 Categories

| ID | Category | Examples |
| ---: | --- | --- |
| `0x01` | SYSTEM | initialization, resource, watchdog, internal invariant |
| `0x02` | USB_TRANSPORT | enumerate, open, read/write, removal, access |
| `0x03` | PROTOCOL | frame, CRC, sequence, command, state, version, capability |
| `0x04` | IMAGE | parse, address, overlap, duplicate, unsupported, compatibility |
| `0x05` | INTEGRITY_SECURITY | CRC/hash/signature/auth/rollback/project lock |
| `0x06` | STORAGE | capacity, geometry, Flash, journal, wear, recovery |
| `0x07` | PROGRAMMING | job, preflight, cancel, verify, target release |
| `0x08` | DEVICE_DRIVER | profile, signature, capability, target status |
| `0x09` | TARGET_LINK | UART, TOOL0, GPIO/reset, timeout, direction, framing |
| `0x0A` | OFFLINE | package state, activation, button, provider |
| `0x0B` | CONFIGURATION | schema, validation, migration, persistence |
| `0x0C` | UPDATE | programmer firmware candidate/apply/rollback |
| `0x0D` | PRODUCTION | operator, barcode, history, counter, export, MES |
| `0x0E` | PLUGIN | discovery, manifest, ABI, publisher, load/quarantine |
| `0x0F` | SDK_API | invalid argument/handle/lifetime/callback/busy |
| `0x10` | FILE_PLATFORM | file/path/permission/disk/Win32 service |
| `0x11` | UART_TRANSPORT | enumerate, open, read/write, removal, timeout |
| `0x12..0x7F` | Reserved standard | Allocation requires registry update |
| `0x80..0xBF` | Vendor | Namespaced extension |
| `0xC0..0xFF` | Reserved | Prohibited |

### 3.3 Component IDs

Component `0x00` is generic within its category. Standard component allocation:

| ID | Component |
| ---: | --- |
| `0x01` | Codec/parser |
| `0x02` | Session/connection |
| `0x03` | Dispatcher/manager |
| `0x04` | Engine/builder |
| `0x05` | Provider/storage adapter |
| `0x06` | Device driver/profile |
| `0x07` | HAL/platform adapter |
| `0x08` | Validator/policy |
| `0x09` | Persistence/history |
| `0x0A` | User/production adapter |
| `0x0B..0xFF` | Allocated by registry update |

### 3.4 Required Standard Errors

The values below are normative. Generated catalogs will reproduce these values; assigned codes are never reused.

| Code | Name | Category | Meaning / retry policy |
| ---: | --- | --- | --- |
| `0x30103001` | `SYSTEM_INTERNAL_INVARIANT` | SYSTEM | Fatal implementation invariant; safe state and reset/latch |
| `0x20100002` | `SYSTEM_RESOURCE_EXHAUSTED` | SYSTEM | Bounded pool/queue unavailable; retry only after resource recovery |
| `0x30103003` | `SYSTEM_WATCHDOG_RESET` | SYSTEM | Prior operation interrupted; never PASS |
| `0x20207001` | `USB_DEVICE_NOT_FOUND` | USB_TRANSPORT | No matching instance; rediscovery allowed |
| `0x20207002` | `USB_ACCESS_DENIED` | USB_TRANSPORT | OS ownership/permission issue; operator action |
| `0x20207003` | `USB_DEVICE_REMOVED` | USB_TRANSPORT | Reconnect and reconcile by UUID |
| `0x20207004` | `USB_IO_TIMEOUT` | USB_TRANSPORT | Retry only by PPS classification |
| `0x20301001` | `PROTOCOL_BAD_MAGIC` | PROTOCOL | Malformed frame; drop |
| `0x20301002` | `PROTOCOL_BAD_LENGTH` | PROTOCOL | Malformed frame/payload; reject/drop |
| `0x20301003` | `PROTOCOL_CRC_MISMATCH` | PROTOCOL | Drop; sender timeout/retry |
| `0x20301004` | `PROTOCOL_INVALID_FLAGS` | PROTOCOL | Reject without side effect |
| `0x20302001` | `PROTOCOL_SEQUENCE_CONFLICT` | PROTOCOL | Same sequence/different content; new session/operator diagnosis |
| `0x20302002` | `PROTOCOL_VERSION_INCOMPATIBLE` | PROTOCOL | No common major/minor; not retryable unchanged |
| `0x20303001` | `PROTOCOL_COMMAND_UNSUPPORTED` | PROTOCOL | Unknown command; capability/version issue |
| `0x20303002` | `PROTOCOL_CAPABILITY_UNSUPPORTED` | PROTOCOL | Feature known but unavailable |
| `0x20303003` | `PROTOCOL_INVALID_STATE` | PROTOCOL | Command illegal in current state |
| `0x20303004` | `PROTOCOL_CURSOR_STALE` | PROTOCOL | Restart paged query |
| `0x20303005` | `UNKNOWN_REMOTE_DETAIL` | PROTOCOL | Known response with unknown remote detail; preserve numeric detail and do not treat as success |
| `0x20401001` | `IMAGE_FORMAT_INVALID` | IMAGE | Parser/container structural failure |
| `0x20401002` | `IMAGE_CHECKSUM_INVALID` | IMAGE | Source-record checksum failure |
| `0x20408001` | `IMAGE_ADDRESS_OVERFLOW` | IMAGE | Range arithmetic invalid; never clamp |
| `0x20408002` | `IMAGE_ADDRESS_UNSUPPORTED` | IMAGE | Outside exact profile/program space |
| `0x20408003` | `IMAGE_OVERLAP_CONFLICT` | IMAGE | Conflicting bytes overlap |
| `0x20408004` | `IMAGE_DUPLICATE_RECORD` | IMAGE | Duplicate policy violation |
| `0x20408005` | `IMAGE_OPTION_POLICY` | IMAGE | Option/security content violates policy |
| `0x20408006` | `IMAGE_DEVICE_INCOMPATIBLE` | IMAGE | Image/profile/target mismatch; no erase |
| `0x20508001` | `INTEGRITY_CRC_MISMATCH` | INTEGRITY_SECURITY | Report/package/section CRC invalid |
| `0x20508002` | `INTEGRITY_HASH_MISMATCH` | INTEGRITY_SECURITY | Full content hash invalid |
| `0x20508003` | `SECURITY_SIGNATURE_REQUIRED` | INTEGRITY_SECURITY | Unsigned artifact rejected by policy |
| `0x20508004` | `SECURITY_SIGNATURE_INVALID` | INTEGRITY_SECURITY | Verification failed; quarantine |
| `0x20508005` | `SECURITY_UNAUTHORIZED` | INTEGRITY_SECURITY | Role/session/action denied |
| `0x20508006` | `SECURITY_ROLLBACK_BLOCKED` | INTEGRITY_SECURITY | Release/update sequence below policy |
| `0x20605001` | `STORAGE_CAPACITY_INSUFFICIENT` | STORAGE | Package cannot fit usable candidate space |
| `0x20605002` | `STORAGE_OFFSET_MISMATCH` | STORAGE | Resume from returned accepted offset |
| `0x20605003` | `STORAGE_WRITE_FAILED` | STORAGE | Candidate remains non-active; provider recovery |
| `0x20609001` | `STORAGE_JOURNAL_CORRUPT` | STORAGE | Boot recovery/degraded mode |
| `0x20605004` | `STORAGE_NO_VALID_ACTIVE_IMAGE` | STORAGE | Offline start blocked |
| `0x20703001` | `PROGRAM_BUSY` | PROGRAMMING | Exclusive target resource owned |
| `0x20703002` | `PROGRAM_JOB_CONFLICT` | PROGRAMMING | Same job UUID with different intent |
| `0x10703003` | `PROGRAM_CANCEL_PENDING` | PROGRAMMING | Warning/event disposition only; ACK status remains zero and response data says pending |
| `0x20704001` | `PROGRAM_VERIFY_MISMATCH` | PROGRAMMING | Fail/retry only by explicit policy |
| `0x20703004` | `PROGRAM_OUTCOME_UNKNOWN` | PROGRAMMING | Reconciliation deadline; later query required |
| `0x20806001` | `DRIVER_PROFILE_NOT_FOUND` | DEVICE_DRIVER | Exact profile unavailable |
| `0x20806002` | `DRIVER_TARGET_MISMATCH` | DEVICE_DRIVER | Signature not exact; no erase |
| `0x20806003` | `DRIVER_TARGET_STATUS` | DEVICE_DRIVER | Official target status mapped in detail |
| `0x20907001` | `TARGET_LINK_TIMEOUT` | TARGET_LINK | Driver policy decides bounded retry |
| `0x20907002` | `TARGET_LINK_UART_FRAMING` | TARGET_LINK | Recover/re-enter according to driver policy |
| `0x20907003` | `TARGET_LINK_DIRECTION_COLLISION` | TARGET_LINK | TOOL0 electrical/direction issue; safe recovery |
| `0x20A05001` | `OFFLINE_IMAGE_NOT_ACTIVE` | OFFLINE | No executable active package |
| `0x20A03001` | `OFFLINE_TRIGGER_REJECTED` | OFFLINE | Busy/no image/policy/interlock |
| `0x20B08001` | `CONFIG_SCHEMA_UNSUPPORTED` | CONFIGURATION | No supported migration |
| `0x20B08002` | `CONFIG_VALIDATION_FAILED` | CONFIGURATION | Staged snapshot rejected |
| `0x20B09001` | `CONFIG_PERSIST_FAILED` | CONFIGURATION | LKG remains active; visible failure |
| `0x20C03001` | `UPDATE_PACKAGE_INCOMPATIBLE` | UPDATE | Wrong hardware/product/version |
| `0x20C03002` | `UPDATE_APPLY_FAILED` | UPDATE | Rollback/recovery status required |
| `0x20D0A001` | `PRODUCTION_BARCODE_INVALID` | PRODUCTION | Job start blocked by policy |
| `0x20D09001` | `PRODUCTION_HISTORY_COMMIT_FAILED` | PRODUCTION | PASS not presented when durable record is required |
| `0x20D0A002` | `PRODUCTION_MES_UNAVAILABLE` | PRODUCTION | Fail-open/closed/queue by factory policy |
| `0x20E03001` | `PLUGIN_ABI_INCOMPATIBLE` | PLUGIN | Quarantine/disable |
| `0x20E03002` | `PLUGIN_TRUST_REJECTED` | PLUGIN | Publisher/hash/signature policy failure |
| `0x20F00001` | `SDK_INVALID_ARGUMENT` | SDK_API | Immediate, no side effect |
| `0x20F00002` | `SDK_INVALID_HANDLE` | SDK_API | Immediate, no side effect |
| `0x20F00003` | `SDK_OPERATION_BUSY` | SDK_API | Queue or retry according to client policy |
| `0x21007001` | `FILE_PERMISSION_DENIED` | FILE_PLATFORM | Report exact sanitized path/action |
| `0x21007002` | `FILE_DISK_FULL` | FILE_PLATFORM | Candidate/atomic write not committed |
| `0x21107001` | `UART_PORT_NOT_FOUND` | UART_TRANSPORT | Named COM port is absent; rediscovery allowed |
| `0x21107002` | `UART_ACCESS_DENIED` | UART_TRANSPORT | Port is busy, denied, or cannot be configured |
| `0x21107003` | `UART_PORT_REMOVED` | UART_TRANSPORT | VCOM disappeared; reconnect and reconcile by programmer UUID |
| `0x21107004` | `UART_IO_TIMEOUT` | UART_TRANSPORT | Retry only by PPS command classification |
| `0x20100FFF` | `UNKNOWN_UNCLASSIFIED` | SYSTEM | Preserve sanitized origin; never convert to success |

### 3.5 Error Context

The common error context includes operation stage, detail code, retryability, native code, UUID correlations, and up to four typed numeric context values. Text is localized on PC from message keys. Firmware never allocates arbitrary error strings and never echoes untrusted file/manifest strings as an error message.

## 4. Capability Contract

Capabilities are namespaced bitsets or TLVs with stable IDs. A capability has owner, version introduced, optional limits, and dependency set. Required capability absence rejects the operation/package. Version text alone never implies a capability.

### 4.1 Base Capability Bitset

The same 32-bit base bitset is used by PPS `NEGOTIATE`, OIP `required_capabilities`, SDK snapshots, and firmware device info.

| Bit | Name | Contract |
| ---: | --- | --- |
| 0 | `CAP_REPLAY_CACHE` | Duplicate replay-safe request returns cached response |
| 1 | `CAP_ASYNC_EVENTS` | PLP asynchronous event commands supported |
| 2 | `CAP_PAGED_QUERY` | Snapshot/cursor PLP page envelope supported |
| 3 | `CAP_RECONNECT_QUERY` | Job/transfer/update identity can be reconciled after reconnect |
| 4 | `CAP_TRANSFER_RESUME` | Matching package/config/update transfer can resume at accepted offset |
| 5 | `CAP_OFFLINE_PACKAGE` | OIP storage/validation/activation available |
| 6 | `CAP_TARGET_PROGRAM` | Programming Engine and at least one target driver available |
| 7 | `CAP_TARGET_INFORMATION` | Generic signature/device information operations available |
| 8 | `CAP_TARGET_READBACK` | At least one driver may support authorized readback; per-profile policy still required |
| 9 | `CAP_CONFIG_SNAPSHOT` | staged/atomic configuration snapshot supported |
| 10 | `CAP_PROGRAMMER_UPDATE` | at least one separate programmer-firmware operation is supported; detailed capability data distinguishes resident-bootloader handoff from staged/signed update |
| 11 | `CAP_DURABLE_HISTORY` | persistent paged job history supported |
| 12 | `CAP_STATISTICS` | lifetime/lot counters and statistics supported |
| 13 | `CAP_PRODUCTION_CONTEXT` | fragmented operator/lot/barcode/MES context supported |
| 14 | `CAP_PACKAGE_SIGNATURE` | at least one approved OIP signature algorithm available |
| 15 | `CAP_ANTI_ROLLBACK` | release/update rollback policy available |
| 16 | `CAP_ATOMIC_ACTIVATION` | active-package pointer changes atomically/recoverably |
| 17 | `CAP_STORAGE_AB` | at least two usable package slots and A/B retention semantics available |
| 18 | `CAP_MULTI_IMAGE` | deterministic OIP multi-image selection supported |
| 19 | `CAP_COMPRESSION` | at least one OIS compression algorithm supported |
| 20 | `CAP_ENCRYPTION` | at least one OIS encryption algorithm supported |
| 21 | `CAP_OFFLINE_BUTTON` | physical offline start input available |
| 22 | `CAP_OFFLINE_AUTORUN` | policy/interlock-controlled power-on auto-run available |
| 23 | `CAP_AUTH_SESSION` | authenticated/authorized PPS session context supported |
| 24 | `CAP_STRUCTURED_LOG` | structured paged firmware log supported |
| 25 | `CAP_TARGET_RECOVERY` | explicit target reset/recovery operations supported |
| 26 | `CAP_PROGRAMMER_RESET` | acknowledged normal reset of the programmer MCU/application is supported |
| 27..30 | Reserved standard | Sender zero; allocation requires contract update |
| 31 | Vendor-extension-present | Extended vendor TLVs are present; never satisfies a standard required capability |

Capability dependency examples: `CAP_STORAGE_AB` requires `CAP_OFFLINE_PACKAGE|CAP_ATOMIC_ACTIVATION`; `CAP_MULTI_IMAGE` requires `CAP_OFFLINE_PACKAGE`; `CAP_TRANSFER_RESUME` requires `CAP_RECONNECT_QUERY`; signature/rollback claims require their detailed algorithm/policy TLVs.

### 4.2 Extended Capability TLVs

| ID/range | Name | Required value |
| ---: | --- | --- |
| `0x0001` | PLP limits | max payload/inflight/replay/event/page/chunk and timeout limits |
| `0x0002` | OIS support | min/max version, section/transform/algorithm bitsets, max sections/manifest/package |
| `0x0003` | Security support | signature algorithms, trust-store generation, auth modes, rollback scopes |
| `0x0004` | Update support | supported handoff/staged-update operations, updater architecture, application profiles, bank/rollback/candidate limits |
| `0x0100..0x01FF` | Storage provider instance | provider ID/type, geometry, slots, alignment, atomicity, health/wear |
| `0x0200..0x02FF` | Target driver instance | driver/profile UUIDs/versions, operation and link-mode bitsets, limits |
| `0x0300` | Hash/CRC algorithms | registry bitsets and acceleration flags |
| `0x0301` | Compression algorithms | OIS registry bitset and streaming resource limits |
| `0x0302` | Encryption algorithms | OIS registry bitset and key-provider modes |
| `0x0400` | Production support | history/statistics/context/counter limits |
| `0x0401` | Offline UI support | button/LED/auto-run/interlock capability and policy version |
| `0x0500..0x7FFF` | Reserved standard | Not allocated |
| `0x8000..0xBFFF` | Vendor namespaced | Optional only; cannot impersonate a standard capability |
| `0xC000..0xFFFF` | Prohibited | Reject |

Multiple provider/driver instance TLVs are allowed and remain ordered by stable provider/driver instance ID. A base bit is set only when its corresponding detailed TLV is internally valid.

Required capability groups:

- PLP: replay, events, paging, reconnect status.
- Storage: provider type, total/usable/max package, erase/write alignment, slot count, atomic activation, A/B, health.
- Image: OIS range, section types, transforms, hash/signature algorithms, multi-image.
- Target: installed device drivers/profiles, read/info/erase/program/verify/reset/readback.
- Offline: button, auto-run, active image, durable result.
- Update: operation kind, resident-bootloader handoff, application-only limits,
  staged/signed/rollback/recovery claims. `CAP_PROGRAMMER_UPDATE` alone does not
  imply signature, A/B, anti-rollback or bootloader self-update.
- Production: history/statistics/lifetime counters/context.
- Security: authorization/session/package signing/anti-rollback/secure identity.

Capability responses are paged if they exceed one PPS payload. Unknown optional capabilities are displayed/logged numerically; unknown required capability in OIP/config rejects it.

### 4.3 Implemented Bridge Application Update Profiles

The development updater uses a board-selected fixed application profile. These
are bridge-application binaries, not UPI/OIP target images. All unused bytes are
`0xFF`; the final four bytes are a little-endian CRC-32/ISO-HDLC over all prior
bytes in the fixed image. The bootloader remains outside the writable range.

| Profile | Application start | Fixed BIN size | CRC offset in BIN | Absolute CRC address | Reserved CRC erase page |
| --- | ---: | ---: | ---: | ---: | --- |
| M032SE3AE | `0x00004000` | `0x0000BC00` | `0x0000BBFC` | `0x0000FBFC` | `0x0000FA00-0x0000FBFF` |
| M484SIDAE | `0x00004000` | `0x0007C000` | `0x0007BFFC` | `0x0007FFFC` | `0x0007F000-0x0007FFFF` |
| M032 TEST BOARD | `0x00004000` | `0x0000BC00` | `0x0000BBFC` | `0x0000FBFC` | `0x0000FA00-0x0000FBFF` |

The two USB resident bootloaders use development identity `0416:3F00`.
M032 TEST BOARD instead exposes the fixed Nuvoton ISP packet flow on its
external 3.3 V UART and reports little-endian board identity `0x3142544D`
(`MTB1`) in CONNECT response bytes 24-27. An invalid vector table or CRC keeps
the selected USB or UART boot transport available. The current update path
erases/programs only the selected application range; bootloader replacement
requires a debugger/programmer and is not implied by this capability.
Before erase, the host must bind the full HID path or COM port to one
bootloader instance
and match the chosen profile against its reported application geometry. A valid
fixed-size image alone is insufficient; a selected-profile/reported-geometry
mismatch rejects the operation before erase. PDID is probe/display information,
not a hard-coded allowlist in this development updater.

All three profiles also require `CONFIG0.CBS[7:6]=10`; M484 additionally requires
`MBS bit 5=1`. The host validates these reported settings before erase and does
not rewrite CONFIG. Initial debugger provisioning preserves every unrelated
CONFIG bit. Resident boot projects use APROM algorithms `M031_AP_128.FLM`
(M032) and `M481_AP_512.FLM` (M484), never LDROM algorithms.

## 5. Job, Stage and Result Contract

### 5.1 Job Identity

Job UUID identifies one immutable intent. Attempt number identifies each execution/retry of that intent. Reusing a job UUID with different project/image/profile/policy/target expectation is `PROGRAM_JOB_CONFLICT`.

### 5.2 Standard Stages

| ID | Stage |
| ---: | --- |
| 0 | NONE |
| 1 | PREFLIGHT |
| 2 | IMAGE_STAGING |
| 3 | PROVIDER_OPEN |
| 4 | TARGET_CONNECT |
| 5 | TARGET_IDENTIFY |
| 6 | PLAN |
| 7 | ERASE |
| 8 | PROGRAM |
| 9 | VERIFY |
| 10 | SENSITIVE_FINALIZE |
| 11 | TARGET_RESET |
| 12 | DISCONNECT_RECOVERY |
| 13 | RESULT_COMMIT |
| 14 | COMPLETE |
| 15..127 | Reserved standard |
| 128..255 | Driver/vendor detail stage; must map to one standard parent stage |

### 5.3 Terminal Results

PASS, FAIL, CANCELLED, INTERRUPTED, and OUTCOME_UNKNOWN are distinct. PASS requires zero terminal error and all policy-required stages. OUTCOME_UNKNOWN is never included in pass/fail yield until reconciled or disposed by approved factory rule; the disposition remains auditable.

## 6. Structured Logging Contract

### 6.1 Event Envelope

| Field | Requirement |
| --- | --- |
| Schema version | Required |
| UTC timestamp | Required field; may be unavailable flag |
| Monotonic timestamp | Required within process/boot |
| Severity | TRACE, DEBUG, INFO, WARNING, ERROR, FATAL |
| Category | SYSTEM, GUI, SDK, USB, PPS, IMAGE, STORAGE, PROGRAMMING, DRIVER, TARGET_LINK, OFFLINE, UPDATE, SECURITY, PRODUCTION, PLUGIN |
| Component/event ID | Stable numeric/name key |
| Correlation | programmer/session/operation/job/package/image/project/profile UUIDs as applicable |
| Stage/error | Standard stage plus unified error/detail |
| Context | Bounded typed key/value allow-list |
| Build identity | Producer and version/build |

### 6.2 Logging Rules

- INFO records lifecycle and terminal operations; WARNING records recovered/degraded conditions; ERROR records failed action; FATAL records safe-latched/reset conditions.
- TRACE/DEBUG is disabled or bounded in production.
- Passwords, tokens, private keys, full image bytes, unredacted certificates, and personal data outside approved production fields are prohibited.
- Firmware log buffer is bounded; progress may coalesce; a terminal result/error
  summary is retained for host reconciliation. A product advertising
  `CAP_DURABLE_HISTORY` must persist it across reset and power loss.
- The compact offline failure snapshot schema is versioned independently. V1
  records the protocol step, current four-byte unit address/index, transmitted
  byte/bit, link timer state/count, PA.9/PA.10 samples and whether a destructive
  stage was entered. A valid link sample is captured before pin/timer cleanup.
  Runtime firmware retains the newest 64 offline terminal failures in a bounded
  sequence-numbered ring. A later PASS must not clear these records. Ring
  overwrite is explicit through an overflow flag and saturated dropped count;
  reset or power loss clears the ring unless durable history is advertised.
- Repeated errors are rate-limited with occurrence count and first/last timestamps.
- PC presentation localizes at display time; persisted event code remains stable.

## 7. Configuration Contract

Operating policy values are `0=ONLINE_ONLY` and `1=OFFLINE_ARMED`. They are
persisted configuration, not programmer runtime states. Offline Armed does not
change the USB identity or disable HID; the active package UUID and policy
generation remain queryable through PLP when a host is present.

Every configuration document/snapshot carries schema ID/version, generation, producer version, content size/hash or CRC, flags, and namespaces. It supports validate, stage, atomic commit, LKG rollback, and migration report.

Namespaces and authorities:

| Namespace | Owner/authority | Examples |
| --- | --- | --- |
| `device.immutable` | Manufacturing | UUID, HW revision, storage geometry, provisioning IDs |
| `firmware.runtime` | Firmware release/authorized service | bounded timeouts, log level, LED brightness |
| `project.programming` | Project approver | target profile, erase/verify/options, retries |
| `factory.production` | Factory admin | auto-run, operator/barcode/MES/project lock |
| `user.ui` | Operator/user | language, layout, paths |
| `plugin.policy` | Product/security admin | roots, publishers, enabled types |

Unknown safety-critical fields reject. Ordinary unknown optional fields are preserved through round-trip when practical. Configuration never raises firmware hard safety ceilings.

## 8. Version and Compatibility Contract

| Domain | Format | Compatibility |
| --- | --- | --- |
| PC build | `a.b.c.ddd` | Build traceability, not feature inference |
| Firmware | semantic + build | Capability negotiation decides feature use |
| PLP | major/minor | Same major; negotiated minor intersection |
| OIS | major/minor | Same major; optional minor additions capability guarded |
| SDK ABI | major/minor/patch | Same ABI major; sized structures for minor growth |
| Plug-in ABI | major/minor | Same major; manifest/capability validation |
| Device profile | UUID + semantic version + hash | Exact approved profile required |
| Project/config/history schema | schema UUID/name + semantic version | Explicit supported migration only |

Compatibility decisions and capability snapshots are recorded with every job result.

## 9. Production Data Contract

Minimum job-attempt record:

- record schema, job UUID, attempt, station/programmer identity;
- project/image/package/profile UUIDs, versions and hashes;
- target signature/identity and serial/barcode when available/authorized;
- operator/role, lot/batch and MES correlation under privacy policy;
- start/end UTC, monotonic duration, per-stage duration/retries;
- PC/SDK/firmware/PLP/OIS/driver versions;
- terminal result, unified error/detail/stage, warning/recovery summary;
- history commit, export and MES disposition.

Retry attempts are never overwritten. Yield denominator and treatment of CANCELLED/INTERRUPTED/OUTCOME_UNKNOWN are stored with report schema/policy.

## 10. Extension and Allocation Process

An allocation change requires owner, semantic definition, numeric value/range, first supported version, compatibility rule, security/safety impact, errors, logs, unit tests, golden/conformance vectors, and traceability update. Values are never recycled after release, even if a feature is deprecated.

## 11. Risk Analysis

| Risk | Control |
| --- | --- |
| Duplicate truth sources drift | Explicit authority map and generated downstream artifacts |
| Error values collide | Structured allocation and never-reuse policy |
| Versions treated as capabilities | Mandatory capability negotiation |
| Logs leak secrets/IP | Allow-list envelope, redaction and production review |
| Config bypasses safety ceilings | Authority namespaces and firmware hard limits |
| Yield numbers differ by tool | Shared terminal/result/attempt and denominator policy |

## 12. Test Suggestions

- Generate/decode every allocated error and verify category/component/reason extraction.
- Cross-language primitive/UUID/hash/time serialization vectors.
- Unknown capability/config/TLV required-versus-optional behavior.
- Log redaction, rate limiting, overflow, correlation and localization fallback.
- Job UUID conflict, retry attempt counting and terminal result/yield classification.
- Compatibility matrix across every supported version domain.
