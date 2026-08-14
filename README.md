# SM64 Saturn Port

This repository is a Sega Saturn port of Super Mario 64 using the required
4 MiB DRAM cartridge. The assembled playable game—not an isolated subsystem—is
the product.

Start with:

- [Product goal](docs/saturn/PRODUCT_GOAL.md): authoritative milestone,
  acceptance rules, baseline, and cost controls.
- [Current state](STATE.md): current accepted artifact, current failures, and
  the next live experiment.
- [Roadmap](ROADMAP.md): playable vertical milestones.
- [How to](HOWTO.md): artifact-safe build and Ymir workflow.
- [Saturn build guide](docs/saturn/BUILDING.md): toolchain and target details.

Historical plans and evidence under `docs/superpowers/`, `.superpowers/`, and
`docs/saturn/evidence/` preserve engineering context. They do not override the
product goal or authorize work.

The sourceboot `diag-skip-geo` configuration is a default-off, invalid-state
upper-bound diagnostic; it is not a supported game mode.
