#ifndef JACKBOT_SVGA_H
#define JACKBOT_SVGA_H

#include <stdint.h>

/*
 * VMware SVGA II / VMSVGA device (PCI 15ad:0405), as exposed by
 * VirtualBox (--graphicscontroller vmsvga) and VMware. This is the
 * real GPU command channel: 2D via FIFO SVGA_CMD_UPDATE and the
 * SVGA3D pipeline for hardware 3D.
 */

int  svga_gpu_init(void);                 /* detect + FIFO map + caps */
int  svga_gpu_present(void);              /* 1 once the GPU owns scanout */
int  svga_gpu_3d_present(void);           /* 1 if the device reports 3D */
int  svga_gpu_takeover(uint32_t w, uint32_t h,
                       uint64_t *out_fbphys, uint32_t *out_pitch,
                       uint32_t *out_fbsize); /* switch VBE -> native SVGA */
void svga_update_rect(int x, int y, int w, int h); /* submit UPDATE */
void svga_gpu_sync(void);                 /* flush the FIFO to device */
void svga_gpu_report(void);               /* serial capability report */

/* Hardware pointer: the GPU composites the cursor, so there is no
   software drag trail. Only active after a native SVGA takeover. */
void svga_cursor_init(void);              /* build + arm the arrow bitmap */
void svga_cursor_show(int visible);       /* show/hide the hardware cursor */
void svga_cursor_move(int x, int y);      /* position the cursor hotspot */
void svga_cursor_flush(void);             /* submit a cursor-only update */

#endif
