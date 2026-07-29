# M032 package storage

`m032_package_store.*` is the portable storage manager. It requires a backend
with read, page erase, write and optional sync operations. Two metadata pages
form a CRC/generation journal and one data slot holds the candidate/active
package. Before overwriting the slot, the old active identity is durably
invalidated. Finalization checks the OIP header, full SHA-256 and canonical
package CRC before activation is permitted.

`m032_numicro_fmc_storage.*` adapts an explicitly reserved M032 APROM range. It
rejects unaligned ranges, any base below the supplied firmware-end address and
any range outside the supplied APROM size. The application defines no default
partition; use the approved release scatter file and retain map-file evidence.
APROM reads use normal memory-mapped byte access. Page erase and word program
alone use protected FMC commands. This split is required because board/target
attempt preparation may legitimately call `SYS_LockReg()`; `FMC_Read()` would
then be unable to trigger protected `ISPTRG` and could expose stale `ISPDAT`
instead of Package-slot data.
The M032 internal profile uses a 64 KiB slot and does not provide A/B rollback.
A future external NOR backend implements the same `M032StorageBackend` contract.
