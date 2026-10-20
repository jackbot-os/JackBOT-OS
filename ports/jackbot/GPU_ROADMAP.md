# GPU roadmap — system-wide GPU compositing on VMSVGA

The requirement: not one 3D program, but **the whole desktop rendered by
the GPU**, every window composited on hardware, with real 3D available
to applications. This document states the architecture and the honest
status of `kernel/svga.c`.

## Current renderer (today)

- `kernel/fb.c` sets a VBE / Bochs mode (ID 0xb0c0) and owns the
  scanout. All drawing goes to a CPU back buffer and `present_rect`
  does a CPU memcpy of the dirty rectangle into the linear framebuffer.
- `kernel/gpu3d.c` is a **fixed-point software rasterizer** (z-buffer,
  per-face diffuse lighting, back-face culling). It is kept as the
  guaranteed fallback on machines with no GPU command channel.
- Nothing is hardware accelerated yet.

## What was added (G1, this commit)

`kernel/svga.c` + `include/kernel/svga.h`:
- PCI detection of VMSVGA `15ad:0405`.
- SVGA II ID negotiation (read-only, ID restored afterward).
- Reads framebuffer/FIFO geometry, `CAP2` and the SVGA3D device
  capability (`SVGA3D_DEVCAP_3D`), maps the FIFO MMIO window.
- Serial capability report (`[SVGA] ...`).
- The FIFO startup sequence, `SVGA_CMD_UPDATE` submission and the
  `SYNCCPU`/`BUSY` fence are implemented as functions but left in
  probe-only mode so the working VBE display is not disrupted.

## Remaining stages (honest)

- **G2 — GPU owns the scanout.** Switch from VBE mode set to the SVGA
  register sequence (set WIDTH/HEIGHT/BPP, initialize the FIFO,
  `REG_ENABLE=1`); map the SVGA framebuffer; make the compositor call
  `svga_update_rect()` instead of the CPU memcpy. This is real 2D
  acceleration (the host GPU does the blit/refresh). Validate on
  VirtualBox with `--graphicscontroller vmsvga --accelerate3d on`.
- **G3 — SVGA3D context and surfaces.** Create a 3D context
  (`BEGIN_3D`/`CREATE_CONTEXT`), define guest surfaces and GMR buffers,
  port the minimum shader/state/constant command set.
- **G4 — GPU compositor.** Each window becomes a 3D surface; the
  compositor transforms, blends and presents them with SVGA3D draws
  (this is the "everything is GPU-rendered" point). Damage tracking
  drives partial surface updates.
- **G5 — 3D API for applications.** Expose a GLES-style API to JCA /
  Python; the `gpu3d.c` software path stays as the fallback.
- **G6 — other GPUs.** GMA/Intel, Bochs, and a virtio-gpu backend
  behind the same compositor interface.

## Scope note

A full SVGA3D driver is on the scale of the Linux `vmwgfx` driver
(thousands of lines) and G3–G5 are multi-week milestones. The G1
commit makes the device, its command channel and its 3D capability
real and observable, and gives the compositor an explicit hand-off
point; it does not claim GPU rendering is finished.
