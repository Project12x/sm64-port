# Ymir 4 MiB DRAM cartridge capture — 2026-07-19

Ymir supports Sega Saturn DRAM cartridges. This run used its portable profile
with:

```toml
[Cartridge]
Type = "DRAM"
[Cartridge.DRAM]
Capacity = "32Mbit"
```

`32Mbit` is the 4 MiB RAM Cart configuration required by this port. The
rebuilt Castle disc reached the target with the cartridge inserted; the Ymir
window title reported the running SM64 Saturn Castle target at approximately
60 VDP1 FPS. The captured frame is a cart-enabled visual regression, not a
claim of retail timing.

Evidence: [cart-enabled screenshot](screenshots/ymir-m4-cart-enabled-2026-07-19.png).
