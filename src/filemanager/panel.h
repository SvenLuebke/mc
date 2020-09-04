/** \file panel.h
 *  \brief Header: defines WPanel structure
 */

#ifndef MC__PANEL_H
#define MC__PANEL_H

#include <inttypes.h>           /* uintmax_t */
#include <limits.h>             /* MB_LEN_MAX */

#include "lib/global.h"         /* gboolean */
#include "lib/fs.h"             /* MC_MAXPATHLEN */
#include "lib/strutil.h"
#include "lib/widget.h"         /* Widget */
#include "lib/filehighlight.h"

#include "dir.h"                /* dir_list */

/*** typedefs(not structures) and defined constants **********************************************/

#ifdef WITH_TABS
#define TABS_UP_TABSVLINE   2

#define TABS_UP_VLINES   3
#define TABS_BOTTOM_VLINES   2

#define MAX_TAB_TITLE 30        /* 0 means full title */
#define TABS_VISIBLE(p) (((p->tabs.list->next != p->tabs.list) || !tabs_options.hide_tabs) && ((current_panel->list_format != list_long && other_panel->list_format != list_long) || p == current_panel))
#define TABS_UP(p) (tabs_options.bar_position == TOP && TABS_VISIBLE(p))
#define HIGHLIGHT_TAB (t == p->tabs.current->data && p != current_panel && tabs_options.highlight_current_tab)
#define TAB_TITLE(p, tab) get_tab_title(p, (Tab *) tab, MAX_TAB_TITLE)
#endif

#define PANEL(x) ((WPanel *)(x))
#define selection(p) (&(p->dir.list[p->selected]))
#define DEFAULT_USER_FORMAT "half type name | size | perm"

#define LIST_FORMATS 4

#define UP_KEEPSEL ((char *) -1)

/*** enums ***************************************************************************************/

#ifdef WITH_TABS
typedef enum
{
    TABDIR_NEXT,
    TABDIR_PREV,
    TABDIR_FIRST,
    TABDIR_LAST,
    TABDIR_ABSOLUTE
} TabsDirection_t;

enum tabs_scroll_flags
{
    NO_SCROLL = 0,
    SCROLL_LEFT = 1,
    SCROLL_RIGHT = 2,
    SCROLL_BOTH = 3
};
#endif

typedef enum
{
    list_full,                  /* Name, size, perm/date */
    list_brief,                 /* Name */
    list_long,                  /* Like ls -l */
    list_user                   /* User defined */
} list_format_t;

typedef enum
{
    frame_full,                 /* full screen frame */
    frame_half                  /* half screen frame */
} panel_display_t;

typedef enum
{
    UP_OPTIMIZE = 0,
    UP_RELOAD = 1,
    UP_ONLY_CURRENT = 2
} panel_update_flags_t;

/* selection flags */
typedef enum
{
    SELECT_FILES_ONLY = 1 << 0,
    SELECT_MATCH_CASE = 1 << 1,
    SELECT_SHELL_PATTERNS = 1 << 2
} panel_select_flags_t;

/* run mode and params */

enum cd_enum
{
    cd_parse_command,
    cd_exact
};

/*** structures declarations (and typedefs of structures)*****************************************/

#ifdef WITH_TABS
typedef struct TabDisplayInfo
{
    GList *start_tab;
    GList *end_tab;
    int start_idx, end_idx;
    enum tabs_scroll_flags scroll;
} TabDisplayInfo;

typedef struct Tab
{
    char *name;
    vfs_path_t *path;
} Tab;

typedef struct TabsInfo
{
    GList *list;
    GList *current;
    int do_not_delete;
} TabsInfo;

typedef struct RestoredTabs
{
    GList *list;
    GList *current;
    int idx;
    int error;
} RestoredTabs;
#endif

struct format_e;

typedef struct panel_field_struct
{
    const char *id;
    int min_size;
    gboolean expands;
    align_crt_t default_just;
    const char *hotkey;
    const char *title_hotkey;
    gboolean is_user_choice;
    gboolean use_in_user_format;
    const char *(*string_fn) (file_entry_t *, int);
    GCompareFunc sort_routine;  /* used by mouse_sort_col() */
} panel_field_t;

typedef struct
{
    dir_list list;
    vfs_path_t *root_vpath;
} panelized_panel_t;

typedef struct
{
    Widget widget;
    dir_list dir;               /* Directory contents */

    list_format_t list_format;  /* listing type */
    int active;                 /* If panel is currently selected */
    vfs_path_t *cwd_vpath;      /* Current Working Directory */
    vfs_path_t *lwd_vpath;      /* Last Working Directory */
    GList *dir_history;         /* directory history */
    GList *dir_history_current; /* pointer to the current history item */
#ifdef WITH_TABS
    TabsInfo tabs;
#endif
    char *hist_name;            /* directory history name for history file */
    int marked;                 /* Count of marked files */
    int dirs_marked;            /* Count of marked directories */
    uintmax_t total;            /* Bytes in marked files */
    int top_file;               /* The file showed on the top of the panel */
    int selected;               /* Index to the selected file */
    int list_cols;              /* Number of file list columns */
    int brief_cols;             /* Number of columns in case of list_brief format */
    gboolean is_panelized;      /* Flag: special filelisting, can't reload */
    panel_display_t frame_size; /* half or full frame */
    char *filter;               /* File name filter */

    /* sort */
    dir_sort_options_t sort_info;
    const panel_field_t *sort_field;

    int dirty;                  /* Should we redisplay the panel? */

    gboolean user_mini_status;  /* Is user_status_format used */
    char *user_format;          /* User format */
    char *user_status_format[LIST_FORMATS];     /* User format for status line */

    GSList *format;             /* Display format */
    GSList *status_format;      /* Mini status format */

    char *panel_name;           /* The panel name */
    struct stat dir_stat;       /* Stat of current dir: used by execute () */

#ifdef HAVE_CHARSET
    int codepage;               /* panel codepage */
#endif

    gboolean searching;
    char search_buffer[MC_MAXFILENAMELEN];
    char prev_search_buffer[MC_MAXFILENAMELEN];
    char search_char[MB_LEN_MAX];       /*buffer for multibytes characters */
    int search_chpoint;         /*point after last characters in search_char */
    int content_shift;          /* Number of characters of filename need to skip from left side. */
    int max_shift;              /* Max shift for visible part of current panel */
} WPanel;

/*** global variables defined in .c file *********************************************************/

extern panelized_panel_t panelized_panel;

extern hook_t *select_file_hook;

extern mc_fhl_t *mc_filehighlight;

#ifdef WITH_TABS
extern GList *saved_tabs;
#endif

/*** declarations of public functions ************************************************************/

WPanel *panel_sized_empty_new (const char *panel_name, int y, int x, int lines, int cols);
WPanel *panel_sized_with_dir_new (const char *panel_name, int y, int x, int lines, int cols,
                                  const vfs_path_t * vpath);

/*** declarations of public functions ************************************************************/

#ifdef WITH_TABS
/*Tabs functions */
void draw_tabs (WPanel * panel);
void hide_tabs (void);
void create_tab (WPanel * p, TabsDirection_t d, Tab * t);
void change_tab (WPanel * p, TabsDirection_t d,
                 GList * tab /*used only for absolute direction */ );
void destroy_tab (Tab * t);
void destroy_tabs (WPanel * p);
char *get_tab_title (WPanel * p, Tab * t, unsigned int max);
void save_tabs_session (char *title);
void restore_tabs_session (char *title);
void move_tab_to_other_panel (void);
void copy_tab_to_other_panel (void);
void swap_tabs (void);
void process_tab_click (unsigned int x);
void move_tab (WPanel * p, TabsDirection_t d);

TabsDirection_t get_new_tabs_direction (void);
void new_tab (WPanel * panel);
void close_tab (WPanel * p);
void rename_tab (WPanel * p);
int get_tab_index (WPanel * p, GList * t);
GList *get_tab_by_index (WPanel * p, int idx);
RestoredTabs _restore_tabs (FILE * f);
void abort_restore (RestoredTabs restored);
void save_tabs_session_custom (void);
void restore_tabs_session_custom (void);
void _write_tabs (FILE * f, WPanel * p);
void read_line (char *line, int max, FILE * f);
void goto_tab (WPanel * p);
void cut_title (char *title, int max);
TabDisplayInfo *display_info (WPanel * p, Widget * w);
int draw_tab (char *title, int selected);

void set_panel_dirbox_yoffset(int offset);
/*End tabs functions */
#endif

void panel_clean_dir (WPanel * panel);

void panel_reload (WPanel * panel);
void panel_set_sort_order (WPanel * panel, const panel_field_t * sort_order);
void panel_re_sort (WPanel * panel);

#ifdef HAVE_CHARSET
void panel_change_encoding (WPanel * panel);
vfs_path_t *remove_encoding_from_path (const vfs_path_t * vpath);
#endif

void update_panels (panel_update_flags_t flags, const char *current_file);
int set_panel_formats (WPanel * p);

void try_to_select (WPanel * panel, const char *name);

void unmark_files (WPanel * panel);
void select_item (WPanel * panel);

void recalculate_panel_summary (WPanel * panel);
void file_mark (WPanel * panel, int idx, int val);
void do_file_mark (WPanel * panel, int idx, int val);

gboolean do_panel_cd (WPanel * panel, const vfs_path_t * new_dir_vpath, enum cd_enum cd_type);

gsize panel_get_num_of_sortable_fields (void);
char **panel_get_sortable_fields (gsize *);
const panel_field_t *panel_get_field_by_id (const char *);
const panel_field_t *panel_get_field_by_title (const char *);
const panel_field_t *panel_get_field_by_title_hotkey (const char *);
gsize panel_get_num_of_user_possible_fields (void);
char **panel_get_user_possible_fields (gsize *);
void panel_set_cwd (WPanel * panel, const vfs_path_t * vpath);
void panel_set_lwd (WPanel * panel, const vfs_path_t * vpath);

void panel_init (void);
void panel_deinit (void);
gboolean do_cd (const vfs_path_t * new_dir_vpath, enum cd_enum cd_type);

/* --------------------------------------------------------------------------------------------- */
/*** inline functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/**
 * Empty panel creation.
 *
 * @param panel_name name of panel for setup retieving
 *
 * @return new instance of WPanel
 */

static inline WPanel *
panel_empty_new (const char *panel_name)
{
    /* Unknown sizes of the panel at startup */
    return panel_sized_empty_new (panel_name, 0, 0, 1, 1);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Panel creation for specified directory.
 *
 * @param panel_name name of panel for setup retieving
 * @param vpath working panel directory. If NULL then current directory is used
 *
 * @return new instance of WPanel
 */

static inline WPanel *
panel_with_dir_new (const char *panel_name, const vfs_path_t * vpath)
{
    /* Unknown sizes of the panel at startup */
    return panel_sized_with_dir_new (panel_name, 0, 0, 1, 1, vpath);
}


/* --------------------------------------------------------------------------------------------- */
/**
 * Panel creation.
 *
 * @param panel_name name of panel for setup retieving
 *
 * @return new instance of WPanel
 */

static inline WPanel *
panel_new (const char *panel_name)
{
    return panel_with_dir_new (panel_name, NULL);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Panel creation with specified size.
 *
 * @param panel_name name of panel for setup retieving
 * @param y y coordinate of top-left corner
 * @param x x coordinate of top-left corner
 * @param lines vertical size
 * @param cols horizontal size
 *
 * @return new instance of WPanel
 */

static inline WPanel *
panel_sized_new (const char *panel_name, int y, int x, int lines, int cols)
{
    return panel_sized_with_dir_new (panel_name, y, x, lines, cols, NULL);
}

/* --------------------------------------------------------------------------------------------- */

#endif /* MC__PANEL_H */
