# JackBOT OS

Version: 1.0.9 (concept)
Target: x86_64, BIOS + UEFI boot

## Build

```sh
make iso
```

Output: `build/JackBOT-1.0.9.iso`

## Test

```sh
scripts/test_boot_bios.sh
```

## Layout

| Path | Purpose |
|---|---|
| arch/x86_64 | boot asm, linker script |
| kernel | kernel C sources |
| include | shared headers |
| drivers | hardware drivers |
| user | user-space programs |
| apps | GUI applications |
| iso | ISO image tree |
| scripts | build and test scripts |
| spec | feature specification |
