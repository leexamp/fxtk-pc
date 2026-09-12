/**
 * app_desktop_en.c — English translation of the desktop extension demo (final whole-file version)
 * page 5 Input / page 6 Paint / page 7 KeyMouse / page 8 Stress / page 9 Scroll
 */
#include "fxtk.h"
#include "fxtk_image.h"
#include "fxtk_effects.h"
#include "fxtk_desktop.h"
#include "fxtk_fs.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ================= Page 5: Input ================= */
/* (no extra state) */

/* ================= Page 6: Paint ================= */
static fx_image_t *s_pbuf = NULL;
static fx_color_t s_brush = FX_RED;
static int s_px = -1, s_py = -1;

static void on_brush(fx_widget_t *w, void *ud) { s_brush = (fx_color_t)(intptr_t)ud; }
static void on_clear(fx_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (s_pbuf) for (int i = 0; i < s_pbuf->w * s_pbuf->h; i++) s_pbuf->px[i] = FX_WHITE;
    s_px = s_py = -1;
}
static void paint_px(fx_image_t *im, int x, int y) {
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            fx_image_set_px(im, x + dx, y + dy, s_brush);
}
static void paint_line(fx_image_t *im, int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int n = (dx > dy ? dx : dy) + 1;
    for (int i = 0; i < n; i++)
        paint_px(im, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n);
}
static void on_paint(fx_widget_t *w, void *ud)
{
    (void)ud;
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    const int PW = 220, PH = 120;   /* fixed offscreen, stretched blit, no rebuild on resize */
    if (!s_pbuf) { s_pbuf = fx_image_create(PW, PH); if (s_pbuf) on_clear(NULL, NULL); }
    if (!s_pbuf) return;
    int mx, my, mp; fx_touch_state(&mx, &my, &mp);
    if (mp && fx_pressed() == w) {
        int px = (mx - x1) * PW / cw, py = (my - y1) * PH / ch;
        if (s_px >= 0) paint_line(s_pbuf, s_px, s_py, px, py);
        else paint_px(s_pbuf, px, py);
        s_px = px; s_py = py;
    } else { s_px = s_py = -1; }
    fx_draw_image(s_pbuf, 0, 0, cw, ch);
}

/* ================= Page 7: KeyMouse ================= */
static void on_keys_view(fx_widget_t *w, void *ud) {
    (void)ud;
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    fx_set_color(FX_RGB(30, 30, 30)); fx_fill_rect(0, 0, cw - 1, ch - 1);
    char buf[96];
    fx_keyev_t k = fx_last_key();
    if (k.utf8[0]) snprintf(buf, sizeof(buf), "Last key: %s", k.utf8);
    else if (k.key == FX_KEY_BACKSPACE) snprintf(buf, sizeof(buf), "Last key: Backspace");
    else if (k.key == FX_KEY_RETURN) snprintf(buf, sizeof(buf), "Last key: Enter");
    else if (k.key == FX_KEY_ESCAPE) snprintf(buf, sizeof(buf), "Last key: Esc");
    else snprintf(buf, sizeof(buf), "Type any key / move the mouse");
    fx_draw_text_c(10, 12, buf, FX_YELLOW, FX_RGB(30, 30, 30));
    int mx, my, mp; fx_touch_state(&mx, &my, &mp);
    snprintf(buf, sizeof(buf), "Mouse: %d, %d  [%s]", mx, my, mp ? "Down" : "Up");
    fx_draw_text_c(10, 44, buf, FX_GREEN, FX_RGB(30, 30, 30));
}

/* ================= Page 8: Stress ================= */
static int s_mv_clicks = 0;
#define MV_DYN_MAX 4000      /* aligned with widget-pool headroom (FX_MAX_WIDGETS 4096) */
static int s_mv_extra = 0;   /* number of dynamically added buttons */
static fx_widget_t *s_dyn[MV_DYN_MAX];   /* pointer cache, avoids a per-frame linear fx_find scan */
static void on_mv_click(fx_widget_t *w, void *ud) {
    (void)ud;
    s_mv_clicks++;
    char buf[64];
    snprintf(buf, sizeof(buf), "Clicked %d times", s_mv_clicks);
    fx_set_title(w, buf);
}
static void on_mv_extra_click(fx_widget_t *w, void *ud) {
    (void)ud;
    char b[16];
    snprintf(b, sizeof(b), "%d", s_mv_extra);   /* button is narrow; after a click show only its number */
    fx_set_title(w, b);
}
static long s_mv_t = 0;
static void on_move(fx_widget_t *w, void *ud) {
    (void)ud;
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    fx_set_color(FX_RGB(20, 20, 26)); fx_fill_rect(0, 0, cw - 1, ch - 1);
    s_mv_t++;
    int t = (int)s_mv_t;
    fx_repaint_rect(x1, y1, x2, y2);  /* anti-flicker: dirty region = whole canvas, so the new position isn't clipped */
    int total = fxtk_widget_count();
    char hdr[96];
    snprintf(hdr, sizeof(hdr), "Widget move stress t=%d · total widgets %d", t, total);
    fx_draw_text_c(10, 8, hdr, FX_GREEN, FX_RGB(20, 20, 26));

    /* lay out by canvas actual capacity (a fullscreen 1080P fits thousands); +1 per frame when FPS has headroom */
    int cols = (cw - 16) / 24; if (cols < 1) cols = 1;
    int rows = (ch - 46) / 20; if (rows < 1) rows = 1;
    int cap = cols * rows;
    if (fxtk_fps() >= 30 && s_mv_extra < cap && s_mv_extra < MV_DYN_MAX) {
        fx_parent(fx_find("move_cv"));   /* attach under the canvas so it clips with the canvas */
        fx_widget_t *nb = fx_button_new(pixel("88,0", "88,0"), title("Dyn"),
                                        line(9), color(FX_RGB(156, 39, 176)), call(on_mv_extra_click));
        fx_parent(fx_find("tab"));
        if (nb) {
            int nx = 8 + (s_mv_extra % cols) * 24, ny = 30 + (s_mv_extra / cols) * 20;
            fx_widget_fix(nb, x1 + nx, y1 + ny);   /* fixed mode + record baseline, prevents layout reset */
            fx_widget_set_rect(nb, x1 + nx, y1 + ny, x1 + nx + 20, y1 + ny + 16);
            s_dyn[s_mv_extra] = nb;
            s_mv_extra++;
        }
    }
    /* all dynamic buttons drift like other widgets (fixed baseline + sinusoidal disturbance) */
    for (int i = 0; i < s_mv_extra; i++) {
        fx_widget_t *d = s_dyn[i];
        if (!d) continue;
        int bx = x1 + 8 + (i % cols) * 24 + (int)(14 * sinf(t * 0.05f + i * 0.7f));
        int by = y1 + 30 + (i / cols) * 20 + (int)(10 * cosf(t * 0.04f + i * 1.1f));
        fx_widget_set_rect(d, bx, by, bx + 20, by + 16);
    }
    fx_draw_text_c(10, 24, "Auto +1 button per frame when FPS has headroom", FX_LGRAY, FX_RGB(20, 20, 26));

    fx_widget_t *b  = fx_find("mv_btn");
    fx_widget_t *l  = fx_find("mv_lbl");
    fx_widget_t *pr = fx_find("mv_prog");
    fx_widget_t *ck = fx_find("mv_chk");
    if (b) {
        int bx = x1 + 20 + (int)((cw - 170) * (0.5f + 0.5f * sinf(t * 0.023f)));
        int by = y1 + 30 + (int)((ch - 100) * (0.5f + 0.5f * cosf(t * 0.031f)));
        fx_set_value(b, t % 100);
        fx_widget_set_rect(b, bx, by, bx + 120, by + 36);
    }
    if (l) {
        int lx = x1 + 10 + (int)((cw - 130) * (0.5f + 0.5f * cosf(t * 0.017f)));
        int ly = y1 + 24 + (int)((ch - 70) * (0.5f + 0.5f * sinf(t * 0.041f)));
        fx_set_value(l, t % 100);
        fx_widget_set_rect(l, lx, ly, lx + 100, ly + 20);
    }
    if (pr) {
        int px = x1 + 10 + (int)((cw - 180) * (0.5f + 0.5f * sinf(t * 0.013f + 2)));
        int py = y1 + ch - 64 + (int)(18 * sinf(t * 0.05f));
        fx_set_value(pr, t % 100);
        fx_widget_set_rect(pr, px, py, px + 160, py + 14);
    }
    if (ck) {
        int cx = x1 + cw - 150 + (int)(30 * sinf(t * 0.06f));
        int cy = y1 + 30 + (int)((ch - 110) * (0.5f + 0.5f * sinf(t * 0.027f + 1)));
        fx_set_value(ck, (t / 40) & 1);
        fx_widget_set_rect(ck, cx, cy, cx + 110, cy + 22);
    }
    
}

/* ================= Build ================= */
/* ---------- Page 9: canvas self-drawn scroll (rate-limited smoothing + UI scale, correct ratio fullscreen) ---------- */
static void on_scroll_view(fx_widget_t *w, void *ud) {
    (void)ud;
    int x1, y1, x2, y2; fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;
    /* row height/font scale with the UI scale, keeping scrollbar proportions consistent with text at any window size */
    int sc = fxtk_ui_scale();
    int row_h = 40 * sc / 100; if (row_h < 16) row_h = 16;
    int fs = 18 * sc / 100; if (fs < 10) fs = 10;
    int total = 60 * row_h;
    int off = fx_scroll_update(w, total);   /* core scrolling: smooth feel (wheel/interpolation/redraw all in the library) */

    fx_set_color(FX_RGB(245, 245, 245));
    fx_fill_rect(0, 0, cw - 1, ch - 1);
    int first = off / row_h;
    for (int i = first; i < 60; i++) {
        int y = i * row_h - off;
        if (y > ch) break;
        char t[64]; snprintf(t, sizeof(t), "Row %d content (scroll with the wheel)", i + 1);
        if (i & 1) {
            fx_set_color(FX_RGB(33, 150, 243));
            fx_fill_rect(10, y + 4, cw - 20, y + row_h - 6);
            int tw = fxtk_text_width_size(fs, t);
            fxtk_draw_text_size(fs, (cw - tw) / 2, y + 12, t, FX_WHITE, FX_RGB(33, 150, 243));
        } else {
            fxtk_draw_text_size(fs, 10, y + 12, t, FX_RGB(40, 40, 40), FX_RGB(245, 245, 245));
        }
    }
    fx_scrollbar_draw(w, off, total);   /* library scrollbar: track + thumb */
}

static void build_newcomp_page(void);   /* forward declaration */
void build_desktop_pages(void)
{
    fx_parent(fx_find("tab"));

    /* ---- page 5 Input ---- */
    fx_label_new(pixel("93,36", "277,56"), page(5), title("Click the input box and type:"), fgcolor(FX_RGB(51, 51, 51)));
    fx_textedit_new(pixel("93,60", "438,140"), name("edit1"), page(5), title("hello there"));
    fx_textedit_new(pixel("93,148", "438,182"), name("edit2"), page(5), title(""), maxlen(20));
    fx_label_new(pixel("93,190", "438,236"), page(5),
                 title("Drag to select / Ctrl+A C V X / Arrow keys Home End;\nFirst box unlimited, second limited to 20 chars (with counter)."),
                 fgcolor(FX_RGB(120, 120, 120)));

    /* ---- page 6 Paint ---- */
    fx_canvas_new(pixel("93,32", "348,236"), name("paint_cv"), page(6), anim(1), color(FX_WHITE), call(on_paint));
    fx_button_new(pixel("356,40", "404,70"), name("br_r"), page(6), title("Red"), color(FX_RGB(244, 67, 54)), call(on_brush));
    fx_button_new(pixel("411,40", "438,70"), name("br_g"), page(6), title("Green"), color(FX_RGB(76, 175, 80)), call(on_brush));
    fx_button_new(pixel("356,80", "404,110"), name("br_b"), page(6), title("Blue"), color(FX_RGB(33, 150, 243)), call(on_brush));
    fx_button_new(pixel("411,80", "438,110"), name("br_k"), page(6), title("Black"), color(FX_RGB(40, 40, 40)), call(on_brush));
    fx_button_new(pixel("356,120", "438,150"), page(6), title("Clear"), color(FX_RGB(150, 150, 150)), call(on_clear));
    fx_set_cb(fx_find("br_r"), on_brush, (void *)(intptr_t)FX_RGB(244, 67, 54));
    fx_set_cb(fx_find("br_g"), on_brush, (void *)(intptr_t)FX_RGB(76, 175, 80));
    fx_set_cb(fx_find("br_b"), on_brush, (void *)(intptr_t)FX_RGB(33, 150, 243));
    fx_set_cb(fx_find("br_k"), on_brush, (void *)(intptr_t)FX_RGB(40, 40, 40));

    /* ---- page 7 KeyMouse ---- */
    fx_canvas_new(pixel("93,32", "438,236"), name("keys_cv"), page(7), anim(1), color(FX_RGB(30, 30, 30)), call(on_keys_view));

    /* ---- page 8 Stress ---- */
    fx_canvas_new(pixel("93,32", "438,236"), name("move_cv"), page(8), anim(1), color(FX_RGB(20, 20, 26)), call(on_move));
    fx_parent(fx_find("move_cv"));   /* moving widgets attach under the canvas: canvas round C2 repaints, no flicker */
    fx_button_new(pixel("167,100", "262,136"), name("mv_btn"), page(8), title("Flying Button"), color(FX_RGB(33, 150, 243)), call(on_mv_click));
    fx_label_new(pixel("167,150", "246,170"), name("mv_lbl"), page(8), title("Drifting Label"), fgcolor(FX_YELLOW));
    fx_progress_new(pixel("167,180", "293,194"), name("mv_prog"), page(8), value(0));
    fx_checkbox_new(pixel("167,200", "253,222"), name("mv_chk"), page(8), title("Toggler"), fgcolor(FX_WHITE));
    fx_parent(fx_find("tab"));

    /* ---- page 9 Scroll (anim: redraw each frame, target + interpolated scroll) ---- */
    fx_canvas_new(pixel("93,32", "438,236"), name("scroll_cv"), page(9), color(FX_RGB(245, 245, 245)), call(on_scroll_view));

    fx_parent(NULL);
    build_newcomp_page();   /* page 11 new components */
}

static char s_names[64][64]; static int s_names_n = 0;   /* dynamic names */
static const char *s_nc_names[] = { "Zhang Wei","Wang Fang","Li Na","Liu Yang","Chen Jing","Yang Fan",
                                    "Zhao Lei","Huang Min","Zhou Tao","Wu Ting","Alice","Bob" };
static void on_nc_pick(fx_widget_t *w, void *ud){ (void)w; int i=(int)(intptr_t)ud;
    fx_widget_t *p=fx_find("nc_picked"); char b[80]; snprintf(b,sizeof(b),"Selected: %s",(i>=0&&i<s_names_n)?s_names[i]:"-"); if(p)fx_set_title(p,b); }
static char         s_fs_dir[512]=""; static fx_fs_entry_t s_ent[64]; static int s_n=0, s_sel=-1, s_hov=-1;
static int          s_dbx[3]={20,150,90}, s_dby[3]={20,20,44}, s_drag_i=-1;
static fx_widget_t *s_nclist_w=0;
static void names_init(void){ if(s_names_n)return;
    for(int i=0;i<12 && s_nc_names[i];i++){ strncpy(s_names[i],s_nc_names[i],63); s_names[i][63]=0; s_names_n++; } }
static void names_refresh(void){ if(!s_nclist_w)return; fx_list_clear(s_nclist_w); for(int i=0;i<s_names_n;i++) fx_list_add(s_nclist_w,s_names[i]); }
static void on_nc_add(fx_widget_t*w,void*ud){ (void)w;(void)ud;
    fx_widget_t*t=fx_find("nc_txt"); const char*tn=t?fx_textedit_text(t):NULL;
    if(!tn||!tn[0]||s_names_n>=64)return; strncpy(s_names[s_names_n],tn,63); s_names[s_names_n][63]=0; s_names_n++; names_refresh(); }
static void on_nc_del(fx_widget_t*w,void*ud){ (void)w;(void)ud;
    int sel=fx_list_sel(s_nclist_w); if(sel>=0&&sel<s_names_n){ for(int i=sel;i<s_names_n-1;i++)strcpy(s_names[i],s_names[i+1]); s_names_n--; names_refresh(); } }
static void on_fs_pick(fx_widget_t*w,void*ud){ (void)w;(void)ud;
    char d[512]; if(fx_fs_pick_dir(d,sizeof(d))){ strncpy(s_fs_dir,d,511); s_fs_dir[511]=0; s_n=fx_fs_list(s_fs_dir,s_ent,64); fx_set_scroll(fx_find("fs_cv"),0); fx_set_title(fx_find("folder_lbl"),s_fs_dir); fx_repaint(); } }
static void on_fs_view(fx_widget_t*w,void*ud){ (void)w;(void)ud;
    int cw,ch; fx_canvas_size(w,&cw,&ch);
    const int rh=24, TOP=22;
    int content=s_n*rh, visible=ch-TOP, maxsc=content-visible; if(maxsc<0)maxsc=0;
    int off = fx_scroll_update(w, content);   /* unified scroll: wheel+ease, returns current offset */
    int mx,my,mp; fx_touch_state(&mx,&my,&mp); int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int lx=mx-x1, ly=my-y1;
    int sbt_h=ch-TOP, thumb_h=maxsc>0?(sbt_h*visible/content):sbt_h; if(thumb_h<8)thumb_h=8;
    int sb_hover=(lx>=cw-16); int tw=sb_hover?12:5; int tx=cw-tw-3;
    int thumb_y=TOP+(maxsc>0?(sbt_h-thumb_h)*off/maxsc:0);
    if(mp && fx_pressed()==w){
        if(lx>=cw-16){ int pos=(ly-TOP); if(pos<0)pos=0; if(pos>sbt_h)pos=sbt_h; if(sbt_h>thumb_h){ fx_set_scroll(w,(int)((long)pos*maxsc/sbt_h)); } }
        else { int idx=(ly-TOP+off)/rh; if(idx>=0&&idx<s_n) s_sel=idx; }
    }
    s_hov=-1; if(lx>=0&&lx<cw&&ly>=0&&ly<ch){ int idx=(ly-TOP+off)/rh; if(idx>=0&&idx<s_n) s_hov=idx; }
    fx_canvas_clear(w,FX_RGB(250,250,250));
    for(int i=0;i<s_n;i++){ int y=TOP+(i*rh)-off; if(y+rh<TOP)continue; if(y>ch-1)break;
        int sel=(i==s_sel), hov=(i==s_hov);
        fx_color_t c=s_ent[i].is_dir?FX_BTN_BLUE:FX_UI_FG;
        fx_set_color(sel?FX_BTN_BLUE:(hov?FX_RGB(210,230,250):FX_RGB(242,242,242))); fx_fill_rect(2,y,cw-2,y+rh-2);
        if(hov && !sel){ fx_set_color(FX_BTN_BLUE); fx_draw_rect(2,y,cw-2,y+rh-2); }
        char nm[64]; strncpy(nm,s_ent[i].name,63); nm[63]=0; if(fx_text_width(nm)>cw/2-14){ nm[cw/2/2-4]=0; strcat(nm,".."); }
        fxtk_draw_text_size(15,6,y,nm,sel?FX_WHITE:c,sel?FX_BTN_BLUE:FX_RGB(250,250,250));
        char sz[32]; snprintf(sz,sizeof(sz),"%lld",s_ent[i].size); fxtk_draw_text_size(15,cw/2,y,sz,sel?FX_WHITE:c,sel?FX_BTN_BLUE:FX_RGB(250,250,250));
        fxtk_draw_text_size(15,cw*3/4,y,s_ent[i].date,sel?FX_WHITE:c,sel?FX_BTN_BLUE:FX_RGB(250,250,250)); }
    /* scrollbar */
    if(maxsc>0){ fx_set_color(FX_RGB(224,224,224)); fx_fill_rect(tx,TOP,cw-2,ch-1);
      fx_set_color(sb_hover?FX_BTN_BLUE:FX_GRAY); fx_fill_rect(tx,thumb_y,tx+tw-1,thumb_y+thumb_h-1); }
    /* header */
    fx_set_color(FX_BTN_BLUE); fx_fill_rect(0,0,cw-1,20); fx_set_color(FX_WHITE);
    fx_draw_text_c(6,4,"Name",FX_WHITE,FX_BTN_BLUE); fx_draw_text_c(cw/2,4,"Size",FX_WHITE,FX_BTN_BLUE); fx_draw_text_c(cw*3/4,4,"Date",FX_WHITE,FX_BTN_BLUE);
    fx_set_color(FX_GRAY); fx_draw_vline(cw/2,22,ch-1); fx_draw_vline(cw*3/4,22,ch-1); fx_draw_rect(0,0,cw-1,ch-1);
    /* hover tooltip (full name/size/date), drawn on top */
    if(s_hov>=0 && s_hov<s_n){
        char tip[160]; snprintf(tip,sizeof(tip),"%s  |  %lld B  |  %s", s_ent[s_hov].name, s_ent[s_hov].size, s_ent[s_hov].date);
        int twd=(fx_text_width(tip)+12<cw-4)?(fx_text_width(tip)+12):(cw-4); int tX=lx+10; int tY=ly+12;
        if(tX+twd>cw)tX=cw-twd-2; if(tX<2)tX=2; if(tY+19>ch)tY=ch-19; if(tY<2)tY=2;   /* follow cursor, in-bounds */
        fx_set_color(FX_RGB(40,40,40)); fx_fill_rect(tX,tY,tX+twd-1,tY+19);
        fx_set_color(FX_WHITE); fx_draw_text_c(tX+5,tY+3,tip,FX_WHITE,FX_RGB(40,40,40));
    }
}



/* font-size slider: change the "example scaled text" label */
static void on_drag(fx_widget_t*w,void*ud){ (void)w;(void)ud;
    int mx,my,mp; fx_touch_state(&mx,&my,&mp);
    int x1,y1,x2,y2; fx_widget_rect(w,&x1,&y1,&x2,&y2);
    int lx=mx-x1, ly=my-y1;   /* canvas local coords */
    int cw,ch; fx_canvas_size(w,&cw,&ch);
    if(mp && fx_pressed()==w){
        if(s_drag_i<0){ int bw[3]={70,58,60}, bh[3]={26,46,26};
            for(int i=0;i<3;i++) if(lx>s_dbx[i]&&lx<s_dbx[i]+bw[i]&&ly>s_dby[i]&&ly<s_dby[i]+bh[i]){ s_drag_i=i; break; } }
        if(s_drag_i>=0){ s_dbx[s_drag_i]=lx-35; s_dby[s_drag_i]=ly-13; }   /* keep dragging until release */
    } else s_drag_i=-1;
    { int bw[3]={70,58,60}, bh[3]={26,46,26};
      for(int i=0;i<3;i++){ if(s_dbx[i]<0)s_dbx[i]=0; if(s_dby[i]<0)s_dby[i]=0;
        if(s_dbx[i]+bw[i]>cw)s_dbx[i]=(cw-bw[i]>0)?cw-bw[i]:0;
        if(s_dby[i]+bh[i]>ch)s_dby[i]=(ch-bh[i]>0)?ch-bh[i]:0; } }   /* clamp to canvas */
    fx_canvas_clear(w,FX_RGB(245,245,245));
    fx_set_color(FX_GRAY); fx_draw_rect(0,0,cw-1,ch-1);
    fx_set_color(FX_BTN_BLUE); fx_fill_rect(s_dbx[0],s_dby[0],s_dbx[0]+70,s_dby[0]+26); fx_draw_text_c(s_dbx[0]+5,s_dby[0]+5,"Drag me",FX_WHITE,FX_BTN_BLUE);
    fx_set_color(FX_OK_GREEN); fx_fill_rect(s_dbx[1],s_dby[1],s_dbx[1]+58,s_dby[1]+46);
    fx_set_color(FX_PURPLE);   fx_fill_rect(s_dbx[2],s_dby[2],s_dbx[2]+60,s_dby[2]+26); fx_draw_text_c(s_dbx[2]+5,s_dby[2]+5,"Draggable components",FX_WHITE,FX_PURPLE);
}

static void on_font_scale(fx_widget_t*w,void*ud){ (void)w;(void)ud;
    int fs=12+fx_get_value(w)*48/100; fx_widget_t*l=fx_find("font_demo"); if(l) fx_set_fontsize(l,fs); fx_repaint(); }
static void build_newcomp_page(void)
{
    names_init();
    fx_parent(fx_find("tab"));
    fx_label_new(pixel("88,30","226,44"), page(10), title("Names"), fgcolor(FX_RGB(40,40,40)));
    fx_grid_map(pixel("88,50","226,70"), line(1), row(2), name("ncedit"), dense(), page(10));
    fx_textedit_new(grid("ncedit",1,1,1,1), name("nc_txt"), page(10), title(""));
    fx_button_new(grid("ncedit",1,2,1,2), page(10), title("+"), color(FX_BTN_BLUE), call(on_nc_add));
    s_nclist_w = fx_list_new_p("88,76","226,180",10); names_refresh();
    fx_list_set_cb(s_nclist_w, on_nc_pick);
    fx_button_new(pixel("88,184","152,204"), page(10), title("Delete"), color(FX_RED_ACCENT), call(on_nc_del));
    fx_label_new(pixel("242,30","459,44"), page(10), title("Original content: freely draggable components (not just buttons)"), fgcolor(FX_RGB(40,40,40)));
    fx_canvas_new(pixel("242,48","459,120"), name("drag_cv"), page(10), anim(1), color(FX_RGB(245,245,245)), call(on_drag));
    fx_button_new(pixel("242,126","352,148"), page(10), title("Pick a folder"), color(FX_BTN_BLUE), call(on_fs_pick));
    fx_label_new(pixel("330,130","459,144"), name("folder_lbl"), page(10), title("(none selected)"), fgcolor(FX_RGB(40,40,40)));
    fx_canvas_new(pixel("242,152","459,236"), name("fs_cv"), page(10), anim(1), color(FX_RGB(250,250,250)), call(on_fs_view));
    fx_label_new(pixel("88,210","226,224"), page(10), name("font_demo"), title("Example scaled text"), fgcolor(FX_RGB(40,40,40)));
    fx_slider_new(pixel("88,226","226,240"), page(10), value(10), call(on_font_scale));
}
