#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <Imlib2.h>
#include <time.h>

#define PROGNAME "xmenu"
#define ITEMPREV 0
#define ITEMNEXT 1

/* macros */
#define LEN(x) (sizeof (x) / sizeof (x[0]))
#define MAX(x,y) ((x)>(y)?(x):(y))
#define MIN(x,y) ((x)<(y)?(x):(y))

/* color enum */
enum {ColorFG, ColorBG, ColorLast};

/* draw context structure */
struct DC {
	XftColor normal[ColorLast];
	XftColor selected[ColorLast];
	XftColor border;
	XftColor separator;

	GC gc;
	XftFont *font;
};

/* menu geometry structure */
struct Geometry {
	int border;             /* window border width */
	int separator;          /* menu separator width */
	int itemw, itemh;       /* item width and height */
	int cursx, cursy;       /* cursor position */
	int screenw, screenh;   /* screen width and height */
};

/* menu item structure */
struct Item {
	char *label;            /* string to be drawed on menu */
	char *output;           /* string to be outputed when item is clicked */
	char *file;             /* filename of the icon */
	int y;                  /* item y position relative to menu */
	int h;                  /* item height */
	size_t labellen;        /* strlen(label) */
	struct Item *prev;      /* previous item */
	struct Item *next;      /* next item */
	struct Menu *submenu;   /* submenu spawned by clicking on item */
	Imlib_Image icon;
};

/* menu structure */
struct Menu {
	struct Menu *parent;    /* parent menu */
	struct Item *caller;    /* item that spawned the menu */
	struct Item *list;      /* list of items contained by the menu */
	struct Item *selected;  /* item currently selected in the menu */
	int x, y, w, h;         /* menu geometry */
	unsigned level;         /* menu level relative to root */
	Drawable pixmap;        /* pixmap to draw the menu on */
	XftDraw *draw;
	Window win;             /* menu window to map on the screen */
};

/* functions declarations */
static void getresources(void);
static void getcolor(const char *s, XftColor *color);
static void setupdc(void);
static void calcgeom(struct Geometry *geom);
static struct Item *allocitem(const char *label, const char *output, char *file);
static struct Menu *allocmenu(struct Menu *parent, struct Item *list, unsigned level);
static struct Menu *buildmenutree(unsigned level, const char *label, const char *output, char *file);
static struct Menu *parsestdin(void);
static Imlib_Image loadicon(const char *file, int size);
static void setupmenusize(struct Geometry *geom, struct Menu *menu);
static void setupmenupos(struct Geometry *geom, struct Menu *menu);
static void setupmenu(struct Geometry *geom, struct Menu *menu, XClassHint *classh);
static void grabpointer(void);
static void grabkeyboard(void);
static struct Menu *getmenu(struct Menu *currmenu, Window win);
static struct Item *getitem(struct Menu *menu, int y);
static void mapmenu(struct Menu *currmenu);
static void drawseparator(struct Menu *menu, struct Item *item);
static void drawitem(struct Menu *menu, struct Item *item, XftColor *color);
static void drawmenu(struct Menu *currmenu);
static struct Item *itemcycle(struct Menu *currmenu, int direction);
static void run(struct Menu *currmenu);
static void freemenu(struct Menu *menu);
static void cleanup(void);
static void usage(void);

/* X stuff */
static Display *dpy;
static int screen;
static Visual *visual;
static Window rootwin;
static Colormap colormap;
static struct DC dc;
static Atom wmdelete;

/* flags */
static int wflag = 0;   /* whether to let the window manager control XMenu */

#include "config.h"

/* xmenu: generate menu from stdin and print selected entry to stdout */
int
main(int argc, char *argv[])
{
	struct Menu *rootmenu;
	struct Geometry geom;
	XClassHint classh;
	int ch;

	while ((ch = getopt(argc, argv, "w")) != -1) {
		switch (ch) {
		case 'w':
			wflag = 1;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();

	/* open connection to server and set X variables */
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "cannot open display");
	screen = DefaultScreen(dpy);
	visual = DefaultVisual(dpy, screen);
	rootwin = RootWindow(dpy, screen);
	colormap = DefaultColormap(dpy, screen);
	wmdelete=XInternAtom(dpy, "WM_DELETE_WINDOW", True);

	/* imlib2 stuff */
	imlib_set_cache_size(2048 * 1024);
	imlib_context_set_dither(1);
	imlib_context_set_display(dpy);
	imlib_context_set_visual(visual);
	imlib_context_set_colormap(colormap);

	/* setup */
	getresources();
	setupdc();
	calcgeom(&geom);

	/* set window class */
	classh.res_class = PROGNAME;
	if (argc == 1)
		classh.res_name = *argv;
	else
		classh.res_name = PROGNAME;

	/* generate menus and set them up */
	rootmenu = parsestdin();
	if (rootmenu == NULL)
		errx(1, "no menu generated");
	setupmenu(&geom, rootmenu, &classh);

	/* grab mouse and keyboard */
	if (!wflag) {
		grabpointer();
		grabkeyboard();
	}

	/* run event loop */
	run(rootmenu);

	/* freeing stuff */
	freemenu(rootmenu);
	cleanup();

	return 0;
}

/* read xrdb for configuration options */
static void
getresources(void)
{
	char *xrm;
	long n;
	char *type;
	XrmDatabase xdb;
	XrmValue xval;

	XrmInitialize();
	if ((xrm = XResourceManagerString(dpy)) == NULL)
		return;

	xdb = XrmGetStringDatabase(xrm);

	if (XrmGetResource(xdb, "xmenu.borderWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			border_pixels = n;
	if (XrmGetResource(xdb, "xmenu.separatorWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			separator_pixels = n;
	if (XrmGetResource(xdb, "xmenu.height", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			height_pixels = n;
	if (XrmGetResource(xdb, "xmenu.width", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			width_pixels = n;
	if (XrmGetResource(xdb, "xmenu.background", "*", &type, &xval) == True)
		background_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xmenu.foreground", "*", &type, &xval) == True)
		foreground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xmenu.selbackground", "*", &type, &xval) == True)
		selbackground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xmenu.selforeground", "*", &type, &xval) == True)
		selforeground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xmenu.separator", "*", &type, &xval) == True)
		separator_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xmenu.border", "*", &type, &xval) == True)
		border_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xmenu.font", "*", &type, &xval) == True)
		font = strdup(xval.addr);

	XrmDestroyDatabase(xdb);
}

/* get color from color string */
static void
getcolor(const char *s, XftColor *color)
{
	if(!XftColorAllocName(dpy, visual, colormap, s, color))
		errx(1, "cannot allocate color: %s", s);
}

/* init draw context */
static void
setupdc(void)
{
	/* get color pixels */
	getcolor(background_color,    &dc.normal[ColorBG]);
	getcolor(foreground_color,    &dc.normal[ColorFG]);
	getcolor(selbackground_color, &dc.selected[ColorBG]);
	getcolor(selforeground_color, &dc.selected[ColorFG]);
	getcolor(background_color,     &dc.separator);
	getcolor(border_color,        &dc.border);

	/* try to get font */
	if ((dc.font = XftFontOpenName(dpy, screen, font)) == NULL)
		errx(1, "cannot load font");

	/* create common GC */
	dc.gc = XCreateGC(dpy, rootwin, 0, NULL);
}

/* calculate menu and screen geometry */
static void
calcgeom(struct Geometry *geom)
{
	Window w1, w2;  /* unused variables */
	int a, b;       /* unused variables */
	unsigned mask;  /* unused variable */

	XQueryPointer(dpy, rootwin, &w1, &w2, &geom->cursx, &geom->cursy, &a, &b, &mask);
	geom->screenw = DisplayWidth(dpy, screen);
	geom->screenh = DisplayHeight(dpy, screen);
	geom->itemh = height_pixels;
	geom->itemw = width_pixels;
	geom->border = border_pixels;
	geom->separator = separator_pixels;
}

/* allocate an item */
static struct Item *
allocitem(const char *label, const char *output, char *file)
{
	struct Item *item;

	if ((item = malloc(sizeof *item)) == NULL)
		err(1, "malloc");
	if (label == NULL) {
		item->label = NULL;
		item->output = NULL;
	} else {
		if ((item->label = strdup(label)) == NULL)
			err(1, "strdup");
		if (label == output) {
			item->output = item->label;
		} else {
			if ((item->output = strdup(output)) == NULL)
				err(1, "strdup");
		}
	}
	if (file == NULL) {
		item->file = NULL;
	} else {
		if ((item->file = strdup(file)) == NULL)
			err(1, "strdup");
	}
	item->y = 0;
	item->h = 0;
	if (item->label == NULL)
		item->labellen = 0;
	else
		item->labellen = strlen(item->label);
	item->next = NULL;
	item->submenu = NULL;
	item->icon = NULL;

	return item;
}

/* allocate a menu */
static struct Menu *
allocmenu(struct Menu *parent, struct Item *list, unsigned level)
{
	XSetWindowAttributes swa;
	struct Menu *menu;

	if ((menu = malloc(sizeof *menu)) == NULL)
		err(1, "malloc");
	menu->parent = parent;
	menu->list = list;
	menu->caller = NULL;
	menu->selected = NULL;
	menu->w = 0;    /* calculated by setupmenu() */
	menu->h = 0;    /* calculated by setupmenu() */
	menu->x = 0;    /* calculated by setupmenu() */
	menu->y = 0;    /* calculated by setupmenu() */
	menu->level = level;

	swa.override_redirect = (wflag) ? False : True;
	swa.background_pixel = dc.normal[ColorBG].pixel;
	swa.border_pixel = dc.border.pixel;
	swa.save_under = True;  /* pop-up windows should save_under*/
	swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask
	               | PointerMotionMask | LeaveWindowMask;
	if (wflag)
		swa.event_mask |= StructureNotifyMask;
	menu->win = XCreateWindow(dpy, rootwin, 0, 0, 1, 1, 0,
	                          CopyFromParent, CopyFromParent, CopyFromParent,
	                          CWOverrideRedirect | CWBackPixel |
	                          CWBorderPixel | CWEventMask | CWSaveUnder,
	                          &swa);

	XSetWMProtocols(dpy, menu->win, &wmdelete, 1);

	return menu;
}

/* build the menu tree */
static struct Menu *
buildmenutree(unsigned level, const char *label, const char *output, char *file)
{
	static struct Menu *prevmenu = NULL;    /* menu the previous item was added to */
	static struct Menu *rootmenu = NULL;    /* menu to be returned */
	struct Item *curritem = NULL;           /* item currently being read */
	struct Item *item;                      /* dummy item for loops */
	struct Menu *menu;                      /* dummy menu for loops */
	unsigned i;

	/* create the item */
	curritem = allocitem(label, output, file);

	/* put the item in the menu tree */
	if (prevmenu == NULL) {                 /* there is no menu yet */
		menu = allocmenu(NULL, curritem, level);
		rootmenu = menu;
		prevmenu = menu;
		curritem->prev = NULL;
	} else if (level < prevmenu->level) {   /* item is continuation of a parent menu */
		/* go up the menu tree until find the menu this item continues */
		for (menu = prevmenu, i = level;
			  menu != NULL && i != prevmenu->level;
			  menu = menu->parent, i++)
			;
		if (menu == NULL)
			errx(1, "reached NULL menu");

		/* find last item in the new menu */
		for (item = menu->list; item->next != NULL; item = item->next)
			;

		prevmenu = menu;
		item->next = curritem;
		curritem->prev = item;
	} else if (level == prevmenu->level) {  /* item is a continuation of current menu */
		/* find last item in the previous menu */
		for (item = prevmenu->list; item->next != NULL; item = item->next)
			;

		item->next = curritem;
		curritem->prev = item;
	} else if (level > prevmenu->level) {   /* item begins a new menu */
		menu = allocmenu(prevmenu, curritem, level);

		/* find last item in the previous menu */
		for (item = prevmenu->list; item->next != NULL; item = item->next)
			;

		prevmenu = menu;
		menu->caller = item;
		item->submenu = menu;
		curritem->prev = NULL;
	}

	return rootmenu;
}

/* create menus and items from the stdin */
static struct Menu *
parsestdin(void)
{
	struct Menu *rootmenu;
	char *s, buf[BUFSIZ];
	char *file, *label, *output;
	unsigned level = 0;

	rootmenu = NULL;

	while (fgets(buf, BUFSIZ, stdin) != NULL) {
		/* get the indentation level */
		level = strspn(buf, "\t");

		/* get the label */
		s = level + buf;
		label = strtok(s, "\t\n");

		/* get the filename */
		file = NULL;
		if (label != NULL && strncmp(label, "IMG:", 4) == 0) {
			file = label + 4;
			label = strtok(NULL, "\t\n");
		}

		/* get the output */
		output = strtok(NULL, "\n");
		if (output == NULL) {
			output = label;
		} else {
			while (*output == '\t')
				output++;
		}

		rootmenu = buildmenutree(level, label, output, file);
	}

	return rootmenu;
}

/* load and scale icon */
static Imlib_Image
loadicon(const char *file, int size)
{
	Imlib_Image icon;
	int width;
	int height;
	int imgsize;

	icon = imlib_load_image(file);
	if (icon == NULL)
		errx(1, "cannot load icon %s", file);

	imlib_context_set_image(icon);

	width = imlib_image_get_width();
	height = imlib_image_get_height();
	imgsize = MIN(width, height);

	icon = imlib_create_cropped_scaled_image(0, 0, imgsize, imgsize, size, size);

	return icon;
}

/* setup the size of a menu and the position of its items */
static void
setupmenusize(struct Geometry *geom, struct Menu *menu)
{
	XGlyphInfo ext;
	struct Item *item;
	int labelwidth;

	menu->w = geom->itemw;
	for (item = menu->list; item != NULL; item = item->next) {
		item->y = menu->h;

		if (item->label == NULL)   /* height for separator item */
			item->h = geom->separator;
		else
			item->h = geom->itemh;
		menu->h += item->h;

		/* get length of item->label rendered in the font */
		XftTextExtentsUtf8(dpy, dc.font, (XftChar8 *)item->label,
		                   item->labellen, &ext);

		/* set menu width */
		labelwidth = ext.xOff + item->h * 2;
		menu->w = MAX(menu->w, labelwidth);

		/* create icon */
		if (item->file != NULL)
			item->icon = loadicon(item->file, item->h - iconpadding * 2);
	}
}

/* setup the position of a menu */
static void
setupmenupos(struct Geometry *g, struct Menu *menu)
{
	static struct Geometry *geom = NULL;
	int width, height;

	/*
	 * Save the geometry so functions can call setupmenupos() without
	 * having to know the geometry.
	 */
	if (g != NULL)
		geom = g;

	width = menu->w + geom->border * 2;
	height = menu->h + geom->border * 2;
	if (menu->parent == NULL) { /* if root menu, calculate in respect to cursor */
		if (geom->screenw - geom->cursx >= menu->w)
			menu->x = geom->cursx;
		else if (geom->cursx > width)
			menu->x = geom->cursx - width;

		if (geom->screenh - geom->cursy >= height)
			menu->y = geom->cursy;
		else if (geom->screenh > height)
			menu->y = geom->screenh - height;
	} else {                    /* else, calculate in respect to parent menu */
		if (geom->screenw - (menu->parent->x + menu->parent->w + geom->border) >= width)
			menu->x = menu->parent->x + menu->parent->w + geom->border + 10;
		else if (menu->parent->x > menu->w + geom->border)
			menu->x = menu->parent->x - menu->w - geom->border - 10;

		if (geom->screenh - (menu->caller->y + menu->parent->y) > height)
			menu->y = menu->caller->y + menu->parent->y;
		else if (geom->screenh - menu->parent->y > height)
			menu->y = menu->parent->y;
		else if (geom->screenh > height)
			menu->y = geom->screenh - height;
	}
}

/* recursivelly setup menu configuration and its pixmap */
static void
setupmenu(struct Geometry *geom, struct Menu *menu, XClassHint *classh)
{
	struct Item *item;
	XWindowChanges changes;
	XSizeHints sizeh;
	XTextProperty wintitle;

	/* setup size and position of menus */
	setupmenusize(geom, menu);
	setupmenupos(geom, menu);

	/* update menu geometry */
	changes.border_width = geom->border;
	changes.height = menu->h;
	changes.width = menu->w;
	changes.x = menu->x;
	changes.y = menu->y;
	XConfigureWindow(dpy, menu->win, CWBorderWidth | CWWidth | CWHeight | CWX | CWY, &changes);

	/* set window title (used if wflag is on) */
	if (menu->parent == NULL) {
		XStringListToTextProperty(&classh->res_name, 1, &wintitle);
	} else {
		XStringListToTextProperty(&menu->caller->output, 1, &wintitle);
	}

	/* set window manager hints */
	sizeh.flags = PMaxSize | PMinSize;
	sizeh.min_width = sizeh.max_width = menu->w;
	sizeh.min_height = sizeh.max_height = menu->h;
	XSetWMProperties(dpy, menu->win, &wintitle, NULL, NULL, 0, &sizeh, NULL, classh);

	/* create pixmap and XftDraw */
	menu->pixmap = XCreatePixmap(dpy, menu->win, menu->w, menu->h,
	                             DefaultDepth(dpy, screen));
	menu->draw = XftDrawCreate(dpy, menu->pixmap, visual, colormap);

	/* calculate positions of submenus */
	for (item = menu->list; item != NULL; item = item->next) {
		if (item->submenu != NULL)
			setupmenu(geom, item->submenu, classh);
	}
}

/* try to grab pointer, we may have to wait for another process to ungrab */
static void
grabpointer(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	for (i = 0; i < 1000; i++) {
		if (XGrabPointer(dpy, rootwin, True, ButtonPressMask,
		                 GrabModeAsync, GrabModeAsync, None,
		                 None, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	errx(1, "cannot grab keyboard");
}

/* try to grab keyboard, we may have to wait for another process to ungrab */
static void
grabkeyboard(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, rootwin, True, GrabModeAsync,
		                  GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	errx(1, "cannot grab keyboard");
}

/* get menu of given window */
static struct Menu *
getmenu(struct Menu *currmenu, Window win)
{
	struct Menu *menu;

	for (menu = currmenu; menu != NULL; menu = menu->parent)
		if (menu->win == win)
			return menu;

	return NULL;
}

/* get item of given menu and position */
static struct Item *
getitem(struct Menu *menu, int y)
{
	struct Item *item;

	if (menu == NULL)
		return NULL;

	for (item = menu->list; item != NULL; item = item->next)
		if (y >= item->y && y <= item->y + item->h)
			return item;

	return NULL;
}

/* umap previous menus and map current menu and its parents */
static void
mapmenu(struct Menu *currmenu)
{
	static struct Menu *prevmenu = NULL;
	struct Menu *menu, *menu_;
	struct Menu *lcamenu;   /* lowest common ancestor menu */
	unsigned minlevel;      /* level of the closest to root menu */
	unsigned maxlevel;      /* level of the closest to root menu */

	/* do not remap current menu if it wasn't updated*/
	if (prevmenu == currmenu)
		return;

	/* if this is the first time mapping, skip calculations */
	if (prevmenu == NULL) {
		XMapWindow(dpy, currmenu->win);
		prevmenu = currmenu;
		return;
	}

	/* find lowest common ancestor menu */
	minlevel = MIN(currmenu->level, prevmenu->level);
	maxlevel = MAX(currmenu->level, prevmenu->level);
	if (currmenu->level == maxlevel) {
		menu = currmenu;
		menu_ = prevmenu;
	} else {
		menu = prevmenu;
		menu_ = currmenu;
	}
	while (menu->level > minlevel)
		menu = menu->parent;
	while (menu != menu_) {
		menu = menu->parent;
		menu_ = menu_->parent;
	}
	lcamenu = menu;

	/* unmap menus from currmenu (inclusive) until lcamenu (exclusive) */
	for (menu = prevmenu; menu != lcamenu; menu = menu->parent) {
		menu->selected = NULL;
		XUnmapWindow(dpy, menu->win);
	}

	/* map menus from currmenu (inclusive) until lcamenu (exclusive) */
	for (menu = currmenu; menu != lcamenu; menu = menu->parent) {

		if (wflag) {
			setupmenupos(NULL, menu);
			XMoveWindow(dpy, menu->win, menu->x, menu->y);
		}

		XMapWindow(dpy, menu->win);
	}

	prevmenu = currmenu;
}

/* draw separator item */
static void
drawseparator(struct Menu *menu, struct Item *item)
{
	int y;

	y = item->y + item->h/2;

	XSetForeground(dpy, dc.gc, dc.separator.pixel);
	XDrawLine(dpy, menu->pixmap, dc.gc, 0, y, menu->w, y);
}

/* draw regular item */
static void
drawitem(struct Menu *menu, struct Item *item, XftColor *color)
{
	int x, y;

	x = item->h;
	y = item->y + (item->h + dc.font->ascent) / 2;
	XSetForeground(dpy, dc.gc, color[ColorFG].pixel);
	XftDrawStringUtf8(menu->draw, &color[ColorFG], dc.font,
                      x, y, (XftChar8 *)item->label, item->labellen);

	/* draw triangle, if item contains a submenu */
	if (item->submenu != NULL) {
		x = menu->w - (item->h + triangle_width + 1) / 2;
		y = item->y + (item->h - triangle_height + 1) / 2;

		XPoint triangle[] = {
			{x, y},
			{x + triangle_width, y + triangle_height/2},
			{x, y + triangle_height},
			{x, y}
		};

		XFillPolygon(dpy, menu->pixmap, dc.gc, triangle, LEN(triangle),
		             Convex, CoordModeOrigin);
	}

	/* draw icon */
	if (item->file != NULL) {
		x = iconpadding;
		y = item->y + iconpadding;
		imlib_context_set_drawable(menu->pixmap);
		imlib_context_set_image(item->icon);
		imlib_render_image_on_drawable(x, y);
	}
}

/* draw items of the current menu and of its ancestors */
static void
drawmenu(struct Menu *currmenu)
{
	struct Menu *menu;
	struct Item *item;

	for (menu = currmenu; menu != NULL; menu = menu->parent) {
		for (item = menu->list; item != NULL; item = item->next) {
			XftColor *color;

			/* determine item color */
			if (item == menu->selected && item->label != NULL)
				color = dc.selected;
			else
				color = dc.normal;

			/* draw item box */
			XSetForeground(dpy, dc.gc, color[ColorBG].pixel);
			XFillRectangle(dpy, menu->pixmap, dc.gc, 0, item->y,
			               menu->w, item->h);

			if (item->label == NULL)  /* item is a separator */
				drawseparator(menu, item);
			else                      /* item is a regular item */
				drawitem(menu, item, color);
		}

		XCopyArea(dpy, menu->pixmap, menu->win, dc.gc, 0, 0,
			      menu->w, menu->h, 0, 0);
	}
}

/* cycle through the items; non-zero direction is next, zero is prev */
static struct Item *
itemcycle(struct Menu *currmenu, int direction)
{
	struct Item *item;
	struct Item *lastitem;

	item = NULL;

	if (direction == ITEMNEXT) {
		if (currmenu->selected == NULL)
			item = currmenu->list;
		else if (currmenu->selected->next != NULL)
			item = currmenu->selected->next;

		while (item != NULL && item->label == NULL)
			item = item->next;

		if (item == NULL)
			item = currmenu->list;
	} else {
		for (lastitem = currmenu->list;
		     lastitem != NULL && lastitem->next != NULL;
		     lastitem = lastitem->next)
			;

		if (currmenu->selected == NULL)
			item = lastitem;
		else if (currmenu->selected->prev != NULL)
			item = currmenu->selected->prev;

		while (item != NULL && item->label == NULL)
			item = item->prev;

		if (item == NULL)
			item = lastitem;
	}

	return item;
}

/* run event loop */
static void
run(struct Menu *currmenu)
{
	struct Menu *menu;
	struct Item *item;
	struct Item *previtem = NULL;
	KeySym ksym;
	XEvent ev;

	mapmenu(currmenu);

	while (!XNextEvent(dpy, &ev)) {
		switch(ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				drawmenu(currmenu);
			break;
		case MotionNotify:
			menu = getmenu(currmenu, ev.xbutton.window);
			item = getitem(menu, ev.xbutton.y);
			if (menu == NULL || item == NULL || previtem == item)
				break;
			previtem = item;
			menu->selected = item;
			if (item->submenu != NULL) {
				currmenu = item->submenu;
				currmenu->selected = NULL;
			} else {
				currmenu = menu;
			}
			mapmenu(currmenu);
			drawmenu(currmenu);
			break;
		case ButtonRelease:
			menu = getmenu(currmenu, ev.xbutton.window);
			item = getitem(menu, ev.xbutton.y);
			if (menu == NULL || item == NULL)
				break;
selectitem:
			if (item->label == NULL)
				break;  /* ignore separators */
			if (item->submenu != NULL) {
				currmenu = item->submenu;
			} else {
				printf("%s\n", item->output);
				return;
			}
			mapmenu(currmenu);
			currmenu->selected = currmenu->list;
			drawmenu(currmenu);
			break;
		case ButtonPress:
			menu = getmenu(currmenu, ev.xbutton.window);
			if (menu == NULL)
				return;
			break;
		case KeyPress:
			ksym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);

			/* esc closes xmenu when current menu is the root menu */
			if (ksym == XK_Escape && currmenu->parent == NULL)
				return;

			/* Shift-Tab = ISO_Left_Tab */
			if (ksym == XK_Tab && (ev.xkey.state & ShiftMask))
				ksym = XK_ISO_Left_Tab;

			/* cycle through menu */
			item = NULL;
			if (ksym == XK_ISO_Left_Tab || ksym == XK_Up) {
				item = itemcycle(currmenu, ITEMPREV);
			} else if (ksym == XK_Tab || ksym == XK_Down) {
				item = itemcycle(currmenu, ITEMNEXT);
			} else if ((ksym == XK_Return || ksym == XK_Right) &&
			           currmenu->selected != NULL) {
				item = currmenu->selected;
				goto selectitem;
			} else if ((ksym == XK_Escape || ksym == XK_Left) &&
			           currmenu->parent != NULL) {
				item = currmenu->parent->selected;
				currmenu = currmenu->parent;
				mapmenu(currmenu);
			} else
				break;
			currmenu->selected = item;
			drawmenu(currmenu);
			break;
		case LeaveNotify:
			previtem = NULL;
			currmenu->selected = NULL;
			drawmenu(currmenu);
			break;
		case ConfigureNotify:
			menu = getmenu(currmenu, ev.xconfigure.window);
			if (menu == NULL)
				break;
			menu->x = ev.xconfigure.x;
			menu->y = ev.xconfigure.y;
			break;
		case ClientMessage:
			/* user closed window */
			menu = getmenu(currmenu, ev.xclient.window);
			if (menu->parent == NULL)
				return;     /* closing the root menu closes the program */
			currmenu = menu->parent;
			mapmenu(currmenu);
			break;
		}
	}
}

/* recursivelly free pixmaps and destroy windows */
static void
freemenu(struct Menu *menu)
{
	struct Item *item;
	struct Item *tmp;

	item = menu->list;
	while (item != NULL) {
		if (item->submenu != NULL)
			freemenu(item->submenu);
		tmp = item;
		if (tmp->label != tmp->output)
			free(tmp->label);
		free(tmp->output);
		if (tmp->file != NULL) {
			free(tmp->file);
			if (tmp->icon != NULL) {
				imlib_context_set_image(tmp->icon);
				imlib_free_image();
			}
		}
		item = item->next;
		free(tmp);
	}

	XFreePixmap(dpy, menu->pixmap);
	XftDrawDestroy(menu->draw);
	XDestroyWindow(dpy, menu->win);
	free(menu);
}

/* cleanup and exit */
static void
cleanup(void)
{
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);

	XftColorFree(dpy, visual, colormap, &dc.normal[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.normal[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.separator);
	XftColorFree(dpy, visual, colormap, &dc.border);

	XFreeGC(dpy, dc.gc);
	XCloseDisplay(dpy);
}

/* show usage */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: xmenu [-w] [title]\n");
	exit(1);
}
