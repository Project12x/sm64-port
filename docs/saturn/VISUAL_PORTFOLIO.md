# Saturn port visual portfolio

Dated screenshots make emulator progress reviewable without confusing a
development-environment result with retail-hardware evidence. Each image is
checked into the repository and identified by SHA-256.

## 2026-07-17 capture set

### Phase 0: hello screen in Yabause HLE

![SM64 Saturn hello screen in Yabause HLE](evidence/screenshots/2026-07-17/hello-yabause-hle.png)

The minimal boot artifact reaches the Saturn display path in the permissive
Yabause development configuration. SHA-256:
`f6c9b1acf25c2a230b4f308e30354da93c572645e20100841ef69c00262d2047`.

### Hardware-test rejection path in Yabause HLE

![SM64 Saturn hardware test in Yabause HLE](evidence/screenshots/2026-07-17/hwtest-yabause-hle.png)

Yabause HLE visibly rejects the absent cartridge (`0x00`) while the
independent VDP1 polygon probe passes. This validates the failure UX and the
telemetry address, not real cartridge behavior. SHA-256:
`e19be1157f3774a9bdd64a056cc33b1c54a4a801905d7173fd456fb74315f6f0`.

### BIOS-backed 4 MiB cartridge path in Kronos

![SM64 Saturn expanded hardware test in Kronos with 4 MiB cartridge](evidence/screenshots/2026-07-17/hwtest-kronos-bios-4m-expanded.png)

Kronos with the user-provided USA BIOS and `4M_extended_ram` reports ID
`0x5C`, passes the destructive 4 MiB memory test, and passes the VDP1 probe.
Its SCU-DMA result is currently `FAIL`; retail hardware must decide whether
that is an emulator limitation or a target-side bug. SHA-256:
`45eacab64f358cac2d1058b7d1b1cd09becd661835729ed99971b67a93cf1d40`.

### BIOS boot control

![Kronos USA BIOS boot control](evidence/screenshots/2026-07-17/kronos-bios-control.png)

This control capture shows Kronos executing the real USA BIOS before the disc
run. It confirms the BIOS-backed configuration; it is not a game result.
SHA-256:
`687167326a799e5629f5f2e9a54a368b9b2e765227b7f808d18c9b01fb9d1cdb`.

## Evidence ledger

| Capture | Emulator/configuration | What it proves | What it does not prove |
|---|---|---|---|
| Hello | Yabause HLE | Boot/display plumbing | Saturn timing or cartridge behavior |
| Hwtest rejection | Yabause HLE, 600 frames | Visible failure path, VDP1 probe, telemetry placement | Retail `0x5C` ID or DRAM timing |
| Hwtest 4 MiB | Kronos, USA BIOS, 4 MiB addon, 3,600 frames | BIOS-backed cart ID and destructive RAM test in an emulator | Retail bus timing; SCU-DMA discrepancy remains open |
| BIOS control | Kronos, USA BIOS | Correct BIOS-backed launch configuration | Disc/game execution |

See [the build procedure](BUILDING.md), [the hardware-test contract](HWTEST.md),
and the per-run [Kronos evidence record](evidence/kronos-hwtest-2026-07-17.md).
