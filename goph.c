#include <err.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#define USED(x) ((void)(x))

typedef union Arg {
	int d;
	double lf;
} Arg;

/* Control actions */
static void page(Arg a);
static void scroll(Arg a);
static void scrollto(Arg a);

#include "config.h"

typedef struct Win {
	Display *dpy;
	int scr;
	Visual *vis;
	Colormap cmap;
	Window win;
	GC gc;

	/* Double buffering */
	Pixmap buf;
	unsigned bufw, bufh;

	XftDraw *drw;
	XftFont *fnt;
	unsigned fnth;
	XftColor fg, bg, scrollfg, scrollbg;

	unsigned w, h;
	int menutop, scrollbar, scrolling;
} Win;

typedef struct Item {
	char type;
	char *name, *sel, *host;
	short port;
} Item;

typedef struct Menu {
	Item *items;
	size_t len, cap;
} Menu;

static Win win;
static Menu menu;

/* Atoms */
Atom wmprotocols;
Atom wmdeletewindow;

/* Event handlers */
static void buttonpress(const XEvent *);
static void buttonrelease(const XEvent *);
static void clientmessage(const XEvent *);
static void configurenotify(const XEvent *);
static void expose(const XEvent *);
static void motionnotify(const XEvent *);

static void (*handlers[])(const XEvent *) = {
	[ButtonPress] = buttonpress,
	[ButtonRelease] = buttonrelease,
	[ClientMessage] = clientmessage,
	[ConfigureNotify] = configurenotify,
	[Expose] = expose,
	[MotionNotify] = motionnotify,
};

/* Utility functions */
static char *estrdup(const char *);

/* Drawing and window functions */
static void additem(char, const char *, const char *, const char *, short);
static void drawbuf(void);
static void drawmenu(void);
static void drawscrollbar(void);
static void makebuf(void);
static void makecolor(const char *, XftColor *);
static void redraw(void);
static void settitle(const char *);
static void xinit(void);

static void page(Arg a)
{
	int pagelines = (win.h + linespace) / (win.fnth + linespace);

	scroll((Arg){ .d = pagelines * a.lf });
}

static void scroll(Arg a)
{
	scrollto((Arg){ .d = win.menutop + a.d });
}

static void scrollto(Arg a)
{
	int oldtop = win.menutop;

	win.menutop = a.d;
	if (win.menutop < 0)
		win.menutop = 0;
	else if (menu.len && win.menutop >= (int)menu.len)
		win.menutop = menu.len - 1;

	if (win.menutop != oldtop)
		redraw();
}

static void
buttonpress(const XEvent *ev)
{
	XButtonEvent e = ev->xbutton;
	int insb = win.scrollbar && e.x >= 0 && e.x <= (int)scrollwidth &&
	    e.y >= 0 && e.y <= (int)win.h;

	switch (e.button) {
	case Button1:
		if (insb)
			page((Arg){ .lf = -(double)e.y / win.h });
		break;
	case Button2:
		if (insb) {
			scrollto((Arg){ .d = e.y * menu.len / win.h });
			win.scrolling = 1;
		}
		break;
	case Button3:
		if (insb)
			page((Arg){ .lf = (double)e.y / win.h });
		break;
	case Button4:
		scroll((Arg){ .d = -1 });
		break;
	case Button5:
		scroll((Arg){ .d = 1 });
		break;
	}
}

static void
buttonrelease(const XEvent *ev)
{
	XButtonEvent e = ev->xbutton;

	if (e.button == Button2)
		win.scrolling = 0;
}

static void
clientmessage(const XEvent *ev)
{
	XClientMessageEvent e = ev->xclient;

	if (e.message_type == wmprotocols && (Atom)e.data.l[0] == wmdeletewindow)
		exit(0);
}

static void
configurenotify(const XEvent *ev)
{
	win.w = ev->xconfigure.width;
	win.h = ev->xconfigure.height;
	redraw();
}

static void
motionnotify(const XEvent *ev)
{
	XMotionEvent e = ev->xmotion;

	if (win.scrolling && (e.state & Button2Mask))
		scrollto((Arg){ .d = e.y * menu.len / win.h });
}

static void
expose(const XEvent *ev)
{
	USED(ev);
	redraw();
}

static char *
estrdup(const char *s)
{
	char *ret;

	if (!(ret = strdup(s)))
		err(1, "strdup");
	return ret;
}

static void
additem(char type, const char *name, const char *sel, const char *host, short port)
{
	Item *i;

	if (menu.len == menu.cap) {
		menu.cap = menu.cap ? 2 * menu.cap : 1;
		if (!(menu.items = reallocarray(menu.items, menu.cap, sizeof(*menu.items))))
			err(1, "reallocarray");
	}

	i = &menu.items[menu.len++];
	i->type = type;
	i->name = estrdup(name);
	i->sel = estrdup(sel);
	i->host = estrdup(host);
	i->port = port;
}

static void
drawbuf(void)
{
	XCopyArea(win.dpy, win.buf, win.win, win.gc,
	    0, 0, win.w, win.h, 0, 0);
}

static void
drawmenu(void)
{
	int startx = margin, x, y;
	size_t i;
	char type[2];

	XSetForeground(win.dpy, win.gc, win.fg.pixel);

	if (win.scrollbar)
		startx += scrollwidth;

	type[1] = 0;
	y = margin;
	for (i = win.menutop; i < menu.len && y < (int)win.h; i++) {
		x = startx;

		if (menu.items[i].type != 'i') {
			type[0] = menu.items[i].type;
			XftDrawStringUtf8(win.drw, &win.fg, win.fnt,
			    x, y + win.fnt -> ascent, type, 1);
		}
		x += win.fnt->max_advance_width * 4;

		XftDrawStringUtf8(win.drw, &win.fg, win.fnt,
		    x, y + win.fnt->ascent,
		    menu.items[i].name, strlen(menu.items[i].name));
		y += win.fnth + linespace;
	}
}

static void
drawscrollbar(void)
{
	unsigned y, h, mh;

	XSetForeground(win.dpy, win.gc, win.scrollbg.pixel);
	XFillRectangle(win.dpy, win.buf, win.gc, 0, 0,
	    scrollwidth, win.h - 1);
	XSetForeground(win.dpy, win.gc, win.scrollfg.pixel);
	XDrawRectangle(win.dpy, win.buf, win.gc, 0, 0,
	    scrollwidth, win.h - 1);

	y = menu.len ? win.h * win.menutop / menu.len : 0;
	mh = margin + menu.len * win.fnth;
	if (menu.len)
		mh += (menu.len - 1) * linespace;
	h = mh > win.h ? win.h * win.h / mh : win.h;
	XFillRectangle(win.dpy, win.buf, win.gc, 1, y,
	    scrollwidth - 2, h);
}

static void
makebuf(void)
{
	if (win.bufw >= win.w && win.bufh >= win.h)
		return;

	if (win.buf)
		XFreePixmap(win.dpy, win.buf);
	win.bufw = win.w;
	win.bufh = win.h;
	win.buf = XCreatePixmap(win.dpy, win.win, win.bufw, win.bufh, 24);

	if (!(win.drw = XftDrawCreate(win.dpy, win.buf, win.vis, win.cmap)))
		errx(1, "cannot create drawing context");
}

static void
makecolor(const char *name, XftColor *c)
{
	if (!XftColorAllocName(win.dpy, win.vis, win.cmap, name, c))
		errx(1, "cannot create color '%s'", name);
}

static void
redraw(void)
{
	makebuf();
	XSetForeground(win.dpy, win.gc, win.bg.pixel);
	XFillRectangle(win.dpy, win.buf, win.gc, 0, 0, win.w, win.h);
	if (win.scrollbar)
		drawscrollbar();
	drawmenu();
	drawbuf();
}

static void
settitle(const char *title)
{
	XTextProperty prop;

	Xutf8TextListToTextProperty(win.dpy, (char **)&title, 1,
	    XUTF8StringStyle, &prop);
	XSetWMName(win.dpy, win.win, &prop);
	XFree(prop.value);
}

static void
xinit(void)
{
	XEvent ev;
	Window ignw;
	int igni;
	unsigned ignu;

	if (!(win.dpy = XOpenDisplay(NULL)))
		errx(1, "cannot open display");
	win.scr = DefaultScreen(win.dpy);
	win.vis = DefaultVisual(win.dpy, win.scr);
	win.cmap = DefaultColormap(win.dpy, win.scr);
	win.win = XCreateWindow(win.dpy, DefaultRootWindow(win.dpy),
	    0, 0, win.w = 480, win.h = 640, /* coordinates and dimensions */
	    0, CopyFromParent, InputOutput, win.vis, 0, NULL);
	win.gc = XCreateGC(win.dpy, win.win, 0, NULL);
	makebuf();

	if (!(win.fnt = XftFontOpenName(win.dpy, win.scr, font)))
		errx(1, "cannot open font %s", font);
	win.fnth = win.fnt->ascent + win.fnt->descent;
	makecolor(fg, &win.fg);
	makecolor(bg, &win.bg);
	makecolor(scrollfg, &win.scrollfg);
	makecolor(scrollbg, &win.scrollbg);
	XSetWindowBackground(win.dpy, win.win, win.bg.pixel);
	XSetBackground(win.dpy, win.gc, win.bg.pixel);
	XSetForeground(win.dpy, win.gc, win.fg.pixel);

	wmprotocols = XInternAtom(win.dpy, "WM_PROTOCOLS", False);
	wmdeletewindow = XInternAtom(win.dpy, "WM_DELETE_WINDOW", False);

	XSetWMProtocols(win.dpy, win.win, &wmdeletewindow, 1);

	XSelectInput(win.dpy, win.win, ExposureMask | StructureNotifyMask |
	    KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
	    Button2MotionMask);
	settitle("goph");
	XMapWindow(win.dpy, win.win);

	win.scrollbar = scrollon;

	/* No drawing can occur until the window is mapped */
	do {
		XNextEvent(win.dpy, &ev);
	} while (ev.type != MapNotify);
	XGetGeometry(win.dpy, win.win, &ignw, &igni, &igni,
	    &win.w, &win.h, &ignu, &ignu);

	redraw();
}

int
main(int argc, char **argv)
{
	XEvent ev;
	int i;
	char tmp[70];

	USED(argc);
	USED(argv);

	xinit();

	additem('i', "Test info", "sel", "host", 0);
	additem('i', "Test info line 2", "sel", "host", 0);
	for (i = 0; i < 50; i++) {
		snprintf(tmp, sizeof(tmp), "Test item %d", i);
		additem('1', tmp, "sel", "host", 0);
	}
	redraw();

	for (;;) {
		XNextEvent(win.dpy, &ev);
		if (handlers[ev.type])
			handlers[ev.type](&ev);
	}

	return 0;
}
