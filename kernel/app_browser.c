#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/kstring.h>
#include <kernel/http.h>
#include <kernel/net.h>
#include <kernel/ps2.h>
#include <kernel/serial.h>

#define MAXLINES 160
#define LCHARS 108
#define HISTN 12

enum { B_IDLE, B_LOADING, B_LOADED, B_FAIL };

struct bline {
    char t[LCHARS + 1];
    int len;
    uint32_t color;
    int link;
    char linkurl[180];
    int hr, img;
};

static struct ui_win W;
static int state, inited, addr_focus, scroll;
static char addr[180];
static int addrlen;
static struct bline lines[MAXLINES];
static int nlines;
static char hist[HISTN][180];
static int hc, hp;
static char curhost[80], curbase[180];

static uint32_t c_text, c_title, c_link, c_dim, c_bg, c_panel;

static void new_line(uint32_t color)
{
    if (nlines >= MAXLINES)
        return;
    memset(&lines[nlines], 0, sizeof(struct bline));
    lines[nlines].color = color;
    nlines++;
}

static void put_char(char c, uint32_t color, int link, const char *url)
{
    if (nlines == 0)
        new_line(color);
    struct bline *l = &lines[nlines - 1];
    if (l->len >= LCHARS) {
        new_line(color);
        l = &lines[nlines - 1];
    }
    l->t[l->len++] = c;
    l->color = color;
    if (link) {
        l->link = 1;
        if (url)
            strncpy(l->linkurl, url, sizeof l->linkurl - 1);
    }
}

static int attr_value(const char *tag, const char *attr, char *out, int max)
{
    int al = (int)strlen(attr);
    for (const char *p = tag; *p; p++) {
        if (!strncmp(p, attr, al) && p[al]=='=') {
            p += al + 1;
            while (*p==' '||*p=='"'||*p=='\'') p++;
            int n = 0;
            while (*p && *p!='"'&&*p!='\''&&*p!=' '&&*p!='>'&&n<max-1)
                out[n++] = *p++;
            out[n] = 0;
            return 1;
        }
    }
    return 0;
}

static int tag_is(const char *tag, const char *name)
{
    const char *t = tag + 1;
    if (*t == '/') t++;
    int nl = (int)strlen(name);
    return !strncmp(t, name, nl) &&
           (t[nl]==' '||t[nl]=='>'||t[nl]=='/'||t[nl]==0);
}

static void entity(const char **p, uint32_t color, int link, const char *url)
{
    const char *q = *p;
    if (!strncmp(q,"&nbsp;",6)) { put_char(' ',color,link,url); q+=6; }
    else if (!strncmp(q,"&amp;",5)) { put_char('&',color,link,url); q+=5; }
    else if (!strncmp(q,"&lt;",4)) { put_char('<',color,link,url); q+=4; }
    else if (!strncmp(q,"&gt;",4)) { put_char('>',color,link,url); q+=4; }
    else { put_char(*q,color,link,url); q++; }
    *p = q;
}

static void layout(const uint8_t *html, int hlen)
{
    nlines = 0;
    const char *p = (const char *)html;
    const char *end = p + hlen;
    int inhead = 0, pre = 0;
    uint32_t col = c_text;
    int link = 0;
    char linkurl[180];
    linkurl[0] = 0;

    new_line(c_text);
    while (p < end) {
        if (*p == '<') {
            char tag[220];
            int tl = 0;
            const char *q = p;
            while (q < end && *q != '>' && tl < 219)
                tag[tl++] = *q++;
            tag[tl] = 0;
            if (q < end)
                q++;
            int closing = tag[1] == '/';

            if (tag_is(tag,"head")||tag_is(tag,"script")||tag_is(tag,"style")) {
                inhead = closing ? 0 : 1;
            } else if (!inhead) {
                if (!closing && (tag_is(tag,"h1")||tag_is(tag,"h2")||
                                 tag_is(tag,"h3"))) {
                    new_line(c_text);
                    col = c_title;
                } else if (closing && (tag_is(tag,"h1")||tag_is(tag,"h2")||
                                      tag_is(tag,"h3"))) {
                    new_line(c_text); col = c_text;
                } else if (!closing && tag_is(tag,"a")) {
                    attr_value(tag,"href",linkurl,sizeof linkurl);
                    col = c_link; link = 1;
                } else if (closing && tag_is(tag,"a")) {
                    col = c_text; link = 0;
                } else if (tag_is(tag,"br")) {
                    new_line(col);
                } else if (!closing && tag_is(tag,"hr")) {
                    new_line(c_text);
                    if (nlines < MAXLINES) lines[nlines-1].hr = 1;
                    new_line(c_text);
                } else if (!closing && tag_is(tag,"img")) {
                    new_line(c_dim);
                    struct bline *l=&lines[nlines-1];
                    strcpy(l->t,"[ image ]"); l->len=9; l->img=1;
                    new_line(c_text);
                } else if (!closing && (tag_is(tag,"p")||tag_is(tag,"div")||
                             tag_is(tag,"li")||tag_is(tag,"tr"))) {
                    new_line(c_text);
                    if (tag_is(tag,"li")) put_char('*',c_dim,0,0);
                } else if (closing && (tag_is(tag,"p")||tag_is(tag,"div"))) {
                    new_line(c_text);
                }
                pre = (!closing && tag_is(tag,"pre"));
            }
            p = q;
            continue;
        }
        if (!inhead) {
            if (*p == '&') {
                entity(&p, col, link, linkurl);
                continue;
            }
            if (*p == '\n' || *p == '\r' || *p == '\t') {
                if (pre && *p=='\n') new_line(col);
                else if (*p=='\t') put_char(' ',col,link,linkurl);
            } else {
                put_char(*p, col, link, linkurl);
            }
        }
        p++;
    }
}

static void resolve_url(const char *rel, char *out)
{
    if (!strncmp(rel,"http://",7)) { strcpy(out,rel); return; }
    if (rel[0] == '/') {
        strcpy(out,"http://");
        strcat(out,curhost);
        strcat(out,rel);
        return;
    }
    strcpy(out,curbase);
    strcat(out,rel);
}

static void load_url(const char *url, int addhist)
{
    if (addhist) {
        hc = hp + 1;
        strncpy(hist[hc], url, 179);
        hist[hc][179] = 0;
        hp = hc;
        hc++;
        if (hc >= HISTN) hc = HISTN - 1;
    }
    const char *p = url + 7;
    int hi = 0;
    curhost[0]=0;
    while (*p && *p != '/' && hi < 79)
        curhost[hi++] = *p++;
    curhost[hi]=0;
    strcpy(curbase,"http://");
    strcat(curbase,curhost);
    const char *slash = strrchr(p,'/');
    if (slash) {
        char dir[160];
        int dl=(int)(slash-p)+1;
        strncpy(dir,p,dl); dir[dl]=0;
        strcat(curbase,dir);
    } else {
        strcat(curbase,"/");
    }

    http_close_client(1);
    http_get_url(1,url);
    state = B_LOADING;
    scroll = 0;
    strncpy(addr,url,179); addr[179]=0; addrlen=(int)strlen(addr);
}

void browser_open(void)
{
    ui_fit(820, 600, &W);
    c_text=fb_rgb(220,226,238); c_title=fb_rgb(255,214,120);
    c_link=fb_rgb(120,190,255); c_dim=fb_rgb(150,160,180);
    c_bg=fb_rgb(26,29,40); c_panel=fb_rgb(36,41,55);
    state=B_IDLE; addr_focus=0; scroll=0;
    hc=0; hp=-1;
    addr[0]=0; addrlen=0;
    inited=1;
    load_url("http://10.0.2.2/",1);
}

static void key_input(void)
{
    char c;
    while (ps2_read_key(&c)) {
        if (c=='\r'||c=='\n') {
            if (addrlen) { char full[180]; resolve_url(addr,full);
                load_url(full,1); addr_focus=0; }
        } else if (c=='\b'||c==0x7f) {
            if (addrlen>0) addr[--addrlen]=0;
        } else if (c>=32 && addrlen<179) {
            addr[addrlen++]=c; addr[addrlen]=0;
        }
    }
}

int browser_frame(int mx, int my, int clicked)
{
    if (!inited)
        browser_open();
    int S = ui_scale();
    if (ui_window(&W,"JBrowser",mx,my,clicked))
        return 1;

    {
        static int ls = -1;
        if (state != ls) {
            serial_puts("[BR] st="); serial_putc('0'+state);
            serial_puts(" http="); serial_putc('0'+http_state(1));
            serial_puts(" body="); serial_puthex(http_body_len(1));
            serial_puts(" nl="); serial_puthex(nlines);
            serial_puts(" scr="); serial_puthex(scroll);
            serial_putc('\n'); ls = state;
        }
    }

    if (addr_focus)
        key_input();

    int top = W.y + 40*S;
    int bx = W.x + 12*S;
    int bw = W.w - 24*S;

    if (ui_button(bx, top, 54*S, 28*S,"<",0,mx,my,clicked))
        if (hp>0) { hp--; load_url(hist[hp],0); }
    if (ui_button(bx+60*S, top, 54*S, 28*S,">",0,mx,my,clicked))
        if (hp<hc-1) { hp++; load_url(hist[hp],0); }
    if (ui_button(bx+120*S, top, 64*S, 28*S,"Reload",0,mx,my,clicked))
        load_url(hist[hp],0);

    int ax = bx + 192*S, aw = bw - 192*S - 44*S;
    ui_rect(ax, top, aw, 28*S, fb_rgb(18,21,30));
    ui_border(ax, top, aw, 28*S, addr_focus?fb_rgb(120,180,255)
                                          :fb_rgb(90,100,130));
    char shown[184];
    strncpy(shown,addr,179); shown[179]=0;
    int sl=(int)strlen(shown);
    if (addr_focus && sl<180) shown[sl]='_', shown[sl+1]=0;
    gfx_draw_string(ax+8*S, top+7*S, c_text, fb_rgb(18,21,30), shown);
    if (clicked && ui_inside(mx,my,ax,top,aw,28*S))
        addr_focus=1;
    else if (clicked)
        addr_focus=0;

    int cy0 = top + 38*S;
    int content_h = W.y + W.h - 20*S - cy0;
    ui_rect(bx, cy0, bw, content_h, c_bg);

    if (state == B_LOADING) {
        http_poll_client(1);
        gfx_draw_string(bx+12*S, cy0+10*S, c_dim, c_bg, "Loading...");
        if (http_ok(1)) {
            layout(http_body(1), http_body_len(1));
            state = B_LOADED;
        } else if (http_failed(1)) {
            state = B_FAIL;
        }
    }

    int lineh = gfx_text_height() + 8*S;
    int maxscroll = nlines*lineh - content_h;
    if (maxscroll < 0) maxscroll = 0;
    if (scroll > maxscroll) scroll = maxscroll;

    if (state == B_LOADED) {
        int first = scroll/lineh;
        for (int i=first; i<nlines; i++) {
            int ry = cy0 + 6*S + i*lineh - scroll;
            if (ry > cy0+content_h) break;
            struct bline *l=&lines[i];
            if (l->hr) {
                for (int xx=bx+10*S; xx<bx+bw-10*S; xx+=2*S)
                    fb_putpixel(xx,ry,c_dim);
                continue;
            }
            if (l->img) {
                ui_rect(bx+14*S,ry,bw-60*S,40*S,c_panel);
                gfx_draw_string(bx+20*S,ry+12*S,c_dim,c_panel,l->t);
                continue;
            }
            gfx_draw_string(bx+14*S,ry,l->color,c_bg,l->t);
            if (l->link && clicked &&
                ui_inside(mx,my,bx+14*S,ry,bw-40*S,lineh)) {
                char full[180];
                resolve_url(l->linkurl,full);
                load_url(full,1);
            }
        }
    } else if (state == B_FAIL) {
        gfx_draw_string(bx+12*S,cy0+10*S,fb_rgb(240,180,120),c_bg,
            "Failed to load page. Check network / URL.");
    }

    int sx = bx + bw - 30*S;
    if (ui_button(sx,cy0,28*S,24*S,"^",0,mx,my,clicked))
        scroll -= lineh*3;
    if (ui_button(sx,cy0+content_h-24*S,28*S,24*S,"v",0,mx,my,clicked))
        scroll += lineh*3;
    if (scroll<0) scroll=0;

    return 0;
}
