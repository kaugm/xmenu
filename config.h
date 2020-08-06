/* font */
static const char *font = "Ubuntu:size=9";    /* for regular items */

/* colors */
static const char *background_color = "#FFFFFF";
static const char *foreground_color = "#2E3436";
static const char *selbackground_color = "#E6E6E6";
static const char *selforeground_color = "#000000";
static const char *separator_color = "#CDC7C2";
static const char *border_color = "#E6E6E6";

/* sizes in pixels */
static int width_pixels = 190;      /* minimum width of a menu */
static int height_pixels = 35;      /* height of a single menu item */
static int border_pixels = 3;       /* menu border */
static int separator_pixels = 15;    /* space around separator */
static int gap_width_pixels = 10;	/* gap between parent and child menus */

/* the variables below cannot be set by X resources */

/* geometry of the right-pointing isoceles triangle for submenus */
static const int triangle_width = 3;
static const int triangle_height = 7;

/* padding of the area around the icon */
static const int iconpadding = 4;


/* custom coloring per pywal - uses mmwm file */
#define COLORS_FILE		"/home/karl/.cache/wal/colors-mmwm.h"
char DUMMY[1000] = "#dfe3e9";		/* default focused window border color */
char SELCOLOR[1000] = "#8e9fc0";		/* default unfocused window border color */