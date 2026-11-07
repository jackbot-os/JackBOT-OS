#ifndef JACKBOT_CURSOR_H
#define JACKBOT_CURSOR_H

/*
 * Unified pointer for every graphical screen (desktop, installer, OOBE).
 *
 *  - On VMSVGA native mode the real GPU composites the cursor
 *    (SVGA_DEFINE_ALPHA_CURSOR / MOVE_CURSOR): it is never drawn into the
 *    frame buffer, so it can never leave a drag trail.
 *  - On Bochs/VBE (no hardware cursor channel) a software cursor is used:
 *    it saves the backing pixels, restores them before the UI repaints,
 *    and redraws the arrow afterwards. The dirty-rectangle presenter
 *    always refreshes both the old and the new spots.
 *
 * Frame protocol:
 *   cursor_begin_frame();   // restore the old site (software path)
 *   ... paint the whole UI ...
 *   cursor_end_frame(mx,my);// draw the arrow (software) or move HW cursor
 *   fb_present_dirty();
 */

#define CUR_ARROW 0
#define CUR_HAND  1
#define CUR_TEXT  2
#define CUR_MOVE  3

void cursor_init(void);
void cursor_set_scale(int s);
void cursor_set_shape(int shape);   /* CUR_ARROW / CUR_HAND / CUR_TEXT / CUR_MOVE */
int  cursor_shape(void);
void cursor_begin_frame(void);
void cursor_end_frame(int mx, int my);
void cursor_show(int visible);

#endif
