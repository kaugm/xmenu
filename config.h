/* font */
static const char *font = "Ubuntu Mono:size=9";    /* for regular items */

/* sizes in pixels */
static int width_pixels = 190;      /* minimum width of a menu */
static int height_pixels = 35;      /* height of a single menu item */
static int border_pixels = 3;       /* menu border */
static int separator_pixels = 25;    /* space around separator */
static int gap_width_pixels = 10;	/* gap between parent and child menus */

/* the variables below cannot be set by X resources */

/* geometry of the right-pointing isoceles triangle for submenus */
static const int triangle_width = 3;
static const int triangle_height = 7;

/* padding of the area around the icon */
static const int iconpadding = 4;

/* custom coloring per pywal - uses mmwm file */
#define COLORS_FILE		"/home/karl/.cache/rpg/colors_xmenu"
char SELCOLOR[8] = "#f5f9fa";		/* default border & selected bg color */
char BGCOLOR[8] = "#000000";		/* default bg color */
char FGCOLOR[8] = "#fffff";		/* default fg and selected fg color */
