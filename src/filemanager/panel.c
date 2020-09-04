/*
   Panel managing.

   Copyright (C) 1994-2020
   Free Software Foundation, Inc.

   Written by:
   Miguel de Icaza, 1995
   Timur Bakeyev, 1997, 1999
   Slava Zanko <slavazanko@gmail.com>, 2013
   Andrew Borodin <aborodin@vmail.ru>, 2013-2016

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file panel.c
 *  \brief Source: panel managin module
 */

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/global.h"

#include "lib/tty/tty.h"
#include "lib/tty/key.h"        /* XCTRL and ALT macros  */
#include "lib/skin.h"
#include "lib/strescape.h"
#include "lib/mcconfig.h"
#include "lib/vfs/vfs.h"
#include "lib/unixcompat.h"
#include "lib/search.h"
#include "lib/timefmt.h"        /* file_date() */
#include "lib/util.h"
#include "lib/widget.h"
#ifdef HAVE_CHARSET
#include "lib/charsets.h"       /* get_codepage_id () */
#endif
#include "lib/event.h"

#include "src/setup.h"          /* For loading/saving panel options */
#include "src/execute.h"
#ifdef HAVE_CHARSET
#include "src/selcodepage.h"    /* select_charset (), SELECT_CHARSET_NO_TRANSLATE */
#endif
#include "src/keybind-defaults.h"       /* global_keymap_t */
#ifdef ENABLE_SUBSHELL
#include "src/subshell/subshell.h"      /* do_subshell_chdir() */
#endif

#include "src/usermenu.h"

#include "dir.h"
#include "boxes.h"
#include "tree.h"
#include "ext.h"                /* regexp_command */
#include "layout.h"             /* Most layout variables are here */
#include "cmd.h"
#include "command.h"            /* cmdline */
#include "midnight.h"
#include "mountlist.h"          /* my_statfs */

#include "panel.h"

#include "../myLog.h"

/*** global variables ****************************************************************************/

/* The hook list for the select file function */
hook_t *select_file_hook = NULL;

/* *INDENT-OFF* */
panelized_panel_t panelized_panel = { {NULL, 0, -1, NULL}, NULL };
/* *INDENT-ON* */

static const char *string_file_name (file_entry_t *, int);
static const char *string_file_size (file_entry_t *, int);
static const char *string_file_size_brief (file_entry_t *, int);
static const char *string_file_type (file_entry_t *, int);
static const char *string_file_mtime (file_entry_t *, int);
static const char *string_file_atime (file_entry_t *, int);
static const char *string_file_ctime (file_entry_t *, int);
static const char *string_file_permission (file_entry_t *, int);
static const char *string_file_perm_octal (file_entry_t *, int);
static const char *string_file_nlinks (file_entry_t *, int);
static const char *string_inode (file_entry_t *, int);
static const char *string_file_nuid (file_entry_t *, int);
static const char *string_file_ngid (file_entry_t *, int);
static const char *string_file_owner (file_entry_t *, int);
static const char *string_file_group (file_entry_t *, int);
static const char *string_marked (file_entry_t *, int);
static const char *string_space (file_entry_t *, int);
static const char *string_dot (file_entry_t *, int);

mc_fhl_t *mc_filehighlight = NULL;

#ifdef WITH_TABS
GList *saved_tabs;
#endif
/*** file scope macro definitions ****************************************************************/

#define NORMAL          0
#define SELECTED        1
#define MARKED          2
#define MARKED_SELECTED 3
#define STATUS          5

/*** file scope type declarations ****************************************************************/

typedef enum
{
    MARK_DONT_MOVE = 0,
    MARK_DOWN = 1,
    MARK_FORCE_DOWN = 2,
    MARK_FORCE_UP = 3
} mark_act_t;

/*
 * This describes a format item.  The parse_display_format routine parses
 * the user specified format and creates a linked list of format_item_t structures.
 */
typedef struct format_item_t
{
    int requested_field_len;
    int field_len;
    align_crt_t just_mode;
    gboolean expand;
    const char *(*string_fn) (file_entry_t *, int len);
    char *title;
    const char *id;
} format_item_t;

/* File name scroll states */
typedef enum
{
    FILENAME_NOSCROLL = 1,
    FILENAME_SCROLL_LEFT = 2,
    FILENAME_SCROLL_RIGHT = 4
} filename_scroll_flag_t;

/*** file scope variables ************************************************************************/

/* *INDENT-OFF* */
static panel_field_t panel_fields[] = {
    {
     "unsorted", 12, TRUE, J_LEFT_FIT,
     /* TRANSLATORS: one single character to represent 'unsorted' sort mode  */
     /* TRANSLATORS: no need to translate 'sort', it's just a context prefix  */
     N_("sort|u"),
     N_("&Unsorted"), TRUE, FALSE,
     string_file_name,
     (GCompareFunc) unsorted
    }
    ,
    {
     "name", 12, TRUE, J_LEFT_FIT,
     /* TRANSLATORS: one single character to represent 'name' sort mode  */
     /* TRANSLATORS: no need to translate 'sort', it's just a context prefix  */
     N_("sort|n"),
     N_("&Name"), TRUE, TRUE,
     string_file_name,
     (GCompareFunc) sort_name
    }
    ,
    {
     "version", 12, TRUE, J_LEFT_FIT,
     /* TRANSLATORS: one single character to represent 'version' sort mode  */
     /* TRANSLATORS: no need to translate 'sort', it's just a context prefix  */
     N_("sort|v"),
     N_("&Version"), TRUE, FALSE,
     string_file_name,
     (GCompareFunc) sort_vers
    }
    ,
    {
     "extension", 12, TRUE, J_LEFT_FIT,
     /* TRANSLATORS: one single character to represent 'extension' sort mode  */
     /* TRANSLATORS: no need to translate 'sort', it's just a context prefix  */
     N_("sort|e"),
     N_("E&xtension"), TRUE, FALSE,
     string_file_name,          /* TODO: string_file_ext */
     (GCompareFunc) sort_ext
    }
    ,
    {
     "size", 7, FALSE, J_RIGHT,
     /* TRANSLATORS: one single character to represent 'size' sort mode  */
     /* TRANSLATORS: no need to translate 'sort', it's just a context prefix  */
     N_("sort|s"),
     N_("&Size"), TRUE, TRUE,
     string_file_size,
     (GCompareFunc) sort_size
    }
    ,
    {
     "bsize", 7, FALSE, J_RIGHT,
     "",
     N_("Block Size"), FALSE, FALSE,
     string_file_size_brief,
     (GCompareFunc) sort_size
    }
    ,
    {
     "type", 1, FALSE, J_LEFT,
     "",
     "", FALSE, TRUE,
     string_file_type,
     NULL
    }
    ,
    {
     "mtime", 12, FALSE, J_RIGHT,
     /* TRANSLATORS: one single character to represent 'Modify time' sort mode  */
     /* TRANSLATORS: no need to translate 'sort', it's just a context prefix  */
     N_("sort|m"),
     N_("&Modify time"), TRUE, TRUE,
     string_file_mtime,
     (GCompareFunc) sort_time
    }
    ,
    {
     "atime", 12, FALSE, J_RIGHT,
     /* TRANSLATORS: one single character to represent 'Access time' sort mode  */
     /* TRANSLATORS: no need to translate 'sort', it's just a context prefix  */
     N_("sort|a"),
     N_("&Access time"), TRUE, TRUE,
     string_file_atime,
     (GCompareFunc) sort_atime
    }
    ,
    {
     "ctime", 12, FALSE, J_RIGHT,
     /* TRANSLATORS: one single character to represent 'Change time' sort mode  */
     /* TRANSLATORS: no need to translate 'sort', it's just a context prefix  */
     N_("sort|h"),
     N_("C&hange time"), TRUE, TRUE,
     string_file_ctime,
     (GCompareFunc) sort_ctime
    }
    ,
    {
     "perm", 10, FALSE, J_LEFT,
     "",
     N_("Permission"), FALSE, TRUE,
     string_file_permission,
     NULL
    }
    ,
    {
     "mode", 6, FALSE, J_RIGHT,
     "",
     N_("Perm"), FALSE, TRUE,
     string_file_perm_octal,
     NULL
    }
    ,
    {
     "nlink", 2, FALSE, J_RIGHT,
     "",
     N_("Nl"), FALSE, TRUE,
     string_file_nlinks, NULL
    }
    ,
    {
     "inode", 5, FALSE, J_RIGHT,
     /* TRANSLATORS: one single character to represent 'inode' sort mode  */
     /* TRANSLATORS: no need to translate 'sort', it's just a context prefix  */
     N_("sort|i"),
     N_("&Inode"), TRUE, TRUE,
     string_inode,
     (GCompareFunc) sort_inode
    }
    ,
    {
     "nuid", 5, FALSE, J_RIGHT,
     "",
     N_("UID"), FALSE, FALSE,
     string_file_nuid,
     NULL
    }
    ,
    {
     "ngid", 5, FALSE, J_RIGHT,
     "",
     N_("GID"), FALSE, FALSE,
     string_file_ngid,
     NULL
    }
    ,
    {
     "owner", 8, FALSE, J_LEFT_FIT,
     "",
     N_("Owner"), FALSE, TRUE,
     string_file_owner,
     NULL
    }
    ,
    {
     "group", 8, FALSE, J_LEFT_FIT,
     "",
     N_("Group"), FALSE, TRUE,
     string_file_group,
     NULL
    }
    ,
    {
     "mark", 1, FALSE, J_RIGHT,
     "",
     " ", FALSE, TRUE,
     string_marked,
     NULL
    }
    ,
    {
     "|", 1, FALSE, J_RIGHT,
     "",
     " ", FALSE, TRUE,
     NULL,
     NULL
    }
    ,
    {
     "space", 1, FALSE, J_RIGHT,
     "",
     " ", FALSE, TRUE,
     string_space,
     NULL
    }
    ,
    {
     "dot", 1, FALSE, J_RIGHT,
     "",
     " ", FALSE, FALSE,
     string_dot,
     NULL
    }
    ,
    {
     NULL, 0, FALSE, J_RIGHT, NULL, NULL, FALSE, FALSE, NULL, NULL
    }
};
/* *INDENT-ON* */

static char *panel_sort_up_char = NULL;
static char *panel_sort_down_char = NULL;

static char *panel_hiddenfiles_show_char = NULL;
static char *panel_hiddenfiles_hide_char = NULL;
static char *panel_history_prev_item_char = NULL;
static char *panel_history_next_item_char = NULL;
static char *panel_history_show_list_char = NULL;
static char *panel_filename_scroll_left_char = NULL;
static char *panel_filename_scroll_right_char = NULL;

/* Panel that selection started */
static WPanel *mouse_mark_panel = NULL;

static gboolean mouse_marking = FALSE;
static int state_mark = 0;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
set_colors (const WPanel * panel)
{
    (void) panel;

    tty_set_normal_attrs ();
    tty_setcolor (NORMAL_COLOR);
}

/* --------------------------------------------------------------------------------------------- */
/** Delete format_item_t object */

static void
format_item_free (format_item_t * format)
{
    g_free (format->title);
    g_free (format);
}

/* --------------------------------------------------------------------------------------------- */
/** Extract the number of available lines in a panel */

static int
panel_lines (const WPanel * p)
{
#ifdef WITH_TABS
    /* 3 lines are: top frame, column header, botton frame */
    return (CONST_WIDGET (p)->lines - 3 - (panels_options.show_mini_info ? 2 : 0) - (TABS_VISIBLE(p) ? (TABS_UP(p) ? TABS_UP_VLINES : TABS_BOTTOM_VLINES) : 0) );
#else
    /* 3 lines are: top frame, column header, botton frame */
    return (CONST_WIDGET (p)->lines - 3 - (panels_options.show_mini_info ? 2 : 0));
#endif
}

/* --------------------------------------------------------------------------------------------- */
/** This code relies on the default justification!!! */

static void
add_permission_string (const char *dest, int width, file_entry_t * fe, int attr, int color,
                       gboolean is_octal)
{
    int i, r, l;

    l = get_user_permissions (&fe->st);

    if (is_octal)
    {
        /* Place of the access bit in octal mode */
        l = width + l - 3;
        r = l + 1;
    }
    else
    {
        /* The same to the triplet in string mode */
        l = l * 3 + 1;
        r = l + 3;
    }

    for (i = 0; i < width; i++)
    {
        if (i >= l && i < r)
        {
            if (attr == SELECTED || attr == MARKED_SELECTED)
                tty_setcolor (MARKED_SELECTED_COLOR);
            else
                tty_setcolor (MARKED_COLOR);
        }
        else if (color >= 0)
            tty_setcolor (color);
        else
            tty_lowlevel_setcolor (-color);

        tty_print_char (dest[i]);
    }
}

/* --------------------------------------------------------------------------------------------- */
/** String representations of various file attributes name */

static const char *
string_file_name (file_entry_t * fe, int len)
{
    static char buffer[MC_MAXPATHLEN * MB_LEN_MAX + 1];

    (void) len;

    g_strlcpy (buffer, fe->fname, sizeof (buffer));
    return buffer;
}

/* --------------------------------------------------------------------------------------------- */

static unsigned int
ilog10 (dev_t n)
{
    unsigned int digits = 0;

    do
    {
        digits++;
        n /= 10;
    }
    while (n != 0);

    return digits;
}

/* --------------------------------------------------------------------------------------------- */

static void
format_device_number (char *buf, size_t bufsize, dev_t dev)
{
    dev_t major_dev, minor_dev;
    unsigned int major_digits, minor_digits;

    major_dev = major (dev);
    major_digits = ilog10 (major_dev);

    minor_dev = minor (dev);
    minor_digits = ilog10 (minor_dev);

    g_assert (bufsize >= 1);

    if (major_digits + 1 + minor_digits + 1 <= bufsize)
        g_snprintf (buf, bufsize, "%lu,%lu", (unsigned long) major_dev, (unsigned long) minor_dev);
    else
        g_strlcpy (buf, _("[dev]"), bufsize);
}

/* --------------------------------------------------------------------------------------------- */
/** size */

static const char *
string_file_size (file_entry_t * fe, int len)
{
    static char buffer[BUF_TINY];

    /* Don't ever show size of ".." since we don't calculate it */
    if (DIR_IS_DOTDOT (fe->fname))
        return _("UP--DIR");

#ifdef HAVE_STRUCT_STAT_ST_RDEV
    if (S_ISBLK (fe->st.st_mode) || S_ISCHR (fe->st.st_mode))
        format_device_number (buffer, len + 1, fe->st.st_rdev);
    else
#endif
        size_trunc_len (buffer, (unsigned int) len, fe->st.st_size, 0, panels_options.kilobyte_si);

    return buffer;
}

/* --------------------------------------------------------------------------------------------- */
/** bsize */

static const char *
string_file_size_brief (file_entry_t * fe, int len)
{
    if (S_ISLNK (fe->st.st_mode) && !link_isdir (fe))
        return _("SYMLINK");

    if ((S_ISDIR (fe->st.st_mode) || link_isdir (fe)) && !DIR_IS_DOTDOT (fe->fname))
        return _("SUB-DIR");

    return string_file_size (fe, len);
}

/* --------------------------------------------------------------------------------------------- */
/** This functions return a string representation of a file entry type */

static const char *
string_file_type (file_entry_t * fe, int len)
{
    static char buffer[2];

    (void) len;

    if (S_ISDIR (fe->st.st_mode))
        buffer[0] = PATH_SEP;
    else if (S_ISLNK (fe->st.st_mode))
    {
        if (link_isdir (fe))
            buffer[0] = '~';
        else if (fe->f.stale_link)
            buffer[0] = '!';
        else
            buffer[0] = '@';
    }
    else if (S_ISCHR (fe->st.st_mode))
        buffer[0] = '-';
    else if (S_ISSOCK (fe->st.st_mode))
        buffer[0] = '=';
    else if (S_ISDOOR (fe->st.st_mode))
        buffer[0] = '>';
    else if (S_ISBLK (fe->st.st_mode))
        buffer[0] = '+';
    else if (S_ISFIFO (fe->st.st_mode))
        buffer[0] = '|';
    else if (S_ISNAM (fe->st.st_mode))
        buffer[0] = '#';
    else if (!S_ISREG (fe->st.st_mode))
        buffer[0] = '?';        /* non-regular of unknown kind */
    else if (is_exe (fe->st.st_mode))
        buffer[0] = '*';
    else
        buffer[0] = ' ';
    buffer[1] = '\0';
    return buffer;
}

/* --------------------------------------------------------------------------------------------- */
/** mtime */

static const char *
string_file_mtime (file_entry_t * fe, int len)
{
    (void) len;

    return file_date (fe->st.st_mtime);
}

/* --------------------------------------------------------------------------------------------- */
/** atime */

static const char *
string_file_atime (file_entry_t * fe, int len)
{
    (void) len;

    return file_date (fe->st.st_atime);
}

/* --------------------------------------------------------------------------------------------- */
/** ctime */

static const char *
string_file_ctime (file_entry_t * fe, int len)
{
    (void) len;

    return file_date (fe->st.st_ctime);
}

/* --------------------------------------------------------------------------------------------- */
/** perm */

static const char *
string_file_permission (file_entry_t * fe, int len)
{
    (void) len;

    return string_perm (fe->st.st_mode);
}

/* --------------------------------------------------------------------------------------------- */
/** mode */

static const char *
string_file_perm_octal (file_entry_t * fe, int len)
{
    static char buffer[10];

    (void) len;

    g_snprintf (buffer, sizeof (buffer), "0%06lo", (unsigned long) fe->st.st_mode);
    return buffer;
}

/* --------------------------------------------------------------------------------------------- */
/** nlink */

static const char *
string_file_nlinks (file_entry_t * fe, int len)
{
    static char buffer[BUF_TINY];

    (void) len;

    g_snprintf (buffer, sizeof (buffer), "%16d", (int) fe->st.st_nlink);
    return buffer;
}

/* --------------------------------------------------------------------------------------------- */
/** inode */

static const char *
string_inode (file_entry_t * fe, int len)
{
    static char buffer[10];

    (void) len;

    g_snprintf (buffer, sizeof (buffer), "%lu", (unsigned long) fe->st.st_ino);
    return buffer;
}

/* --------------------------------------------------------------------------------------------- */
/** nuid */

static const char *
string_file_nuid (file_entry_t * fe, int len)
{
    static char buffer[10];

    (void) len;

    g_snprintf (buffer, sizeof (buffer), "%lu", (unsigned long) fe->st.st_uid);
    return buffer;
}

/* --------------------------------------------------------------------------------------------- */
/** ngid */

static const char *
string_file_ngid (file_entry_t * fe, int len)
{
    static char buffer[10];

    (void) len;

    g_snprintf (buffer, sizeof (buffer), "%lu", (unsigned long) fe->st.st_gid);
    return buffer;
}

/* --------------------------------------------------------------------------------------------- */
/** owner */

static const char *
string_file_owner (file_entry_t * fe, int len)
{
    (void) len;

    return get_owner (fe->st.st_uid);
}

/* --------------------------------------------------------------------------------------------- */
/** group */

static const char *
string_file_group (file_entry_t * fe, int len)
{
    (void) len;

    return get_group (fe->st.st_gid);
}

/* --------------------------------------------------------------------------------------------- */
/** mark */

static const char *
string_marked (file_entry_t * fe, int len)
{
    (void) len;

    return fe->f.marked ? "*" : " ";
}

/* --------------------------------------------------------------------------------------------- */
/** space */

static const char *
string_space (file_entry_t * fe, int len)
{
    (void) fe;
    (void) len;

    return " ";
}

/* --------------------------------------------------------------------------------------------- */
/** dot */

static const char *
string_dot (file_entry_t * fe, int len)
{
    (void) fe;
    (void) len;

    return ".";
}

/* --------------------------------------------------------------------------------------------- */

static int
file_compute_color (int attr, file_entry_t * fe)
{
    switch (attr)
    {
    case SELECTED:
        return (SELECTED_COLOR);
    case MARKED:
        return (MARKED_COLOR);
    case MARKED_SELECTED:
        return (MARKED_SELECTED_COLOR);
    case STATUS:
        return (NORMAL_COLOR);
    case NORMAL:
    default:
        if (!panels_options.filetype_mode)
            return (NORMAL_COLOR);
    }

    return mc_fhl_get_color (mc_filehighlight, fe);
}

/* --------------------------------------------------------------------------------------------- */
/** Returns the number of items in the given panel */

static int
panel_items (const WPanel * p)
{
    return panel_lines (p) * p->list_cols;
}

/* --------------------------------------------------------------------------------------------- */
/** Formats the file number file_index of panel in the buffer dest */

static filename_scroll_flag_t
format_file (WPanel * panel, int file_index, int width, int attr, gboolean isstatus,
             int *field_length)
{
    int color = NORMAL_COLOR;
    int length = 0;
    GSList *format, *home;
    file_entry_t *fe = NULL;
    filename_scroll_flag_t res = FILENAME_NOSCROLL;

    *field_length = 0;

    if (file_index < panel->dir.len)
    {
        fe = &panel->dir.list[file_index];
        color = file_compute_color (attr, fe);
    }

    home = isstatus ? panel->status_format : panel->format;

    for (format = home; format != NULL && length != width; format = g_slist_next (format))
    {
        format_item_t *fi = (format_item_t *) format->data;

        if (fi->string_fn != NULL)
        {
            const char *txt = " ";
            int len, perm = 0;
            const char *prepared_text;
            int name_offset = 0;

            if (fe != NULL)
                txt = fi->string_fn (fe, fi->field_len);

            len = fi->field_len;
            if (len + length > width)
                len = width - length;
            if (len <= 0)
                break;

            if (!isstatus && panel->content_shift > -1 && strcmp (fi->id, "name") == 0)
            {
                int str_len;
                int i;

                *field_length = len + 1;

                str_len = str_length (txt);
                i = MAX (0, str_len - len);
                panel->max_shift = MAX (panel->max_shift, i);
                i = MIN (panel->content_shift, i);

                if (i > -1)
                {
                    name_offset = str_offset_to_pos (txt, i);
                    if (str_len > len)
                    {
                        res = FILENAME_SCROLL_LEFT;
                        if (str_length (txt + name_offset) > len)
                            res |= FILENAME_SCROLL_RIGHT;
                    }
                }
            }

            if (panels_options.permission_mode)
            {
                if (strcmp (fi->id, "perm") == 0)
                    perm = 1;
                else if (strcmp (fi->id, "mode") == 0)
                    perm = 2;
            }

            if (color >= 0)
                tty_setcolor (color);
            else
                tty_lowlevel_setcolor (-color);

            if (!isstatus && panel->content_shift > -1)
                prepared_text = str_fit_to_term (txt + name_offset, len, HIDE_FIT (fi->just_mode));
            else
                prepared_text = str_fit_to_term (txt, len, fi->just_mode);

            if (perm != 0 && fe != NULL)
                add_permission_string (prepared_text, fi->field_len, fe, attr, color, perm != 1);
            else
                tty_print_string (prepared_text);

            length += len;
        }
        else
        {
            if (attr == SELECTED || attr == MARKED_SELECTED)
                tty_setcolor (SELECTED_COLOR);
            else
                tty_setcolor (NORMAL_COLOR);
            tty_print_one_vline (TRUE);
            length++;
        }
    }

    if (length < width)
    {
        int y, x;

        tty_getyx (&y, &x);
        tty_draw_hline (y, x, ' ', width - length);
    }

    return res;
}

/* --------------------------------------------------------------------------------------------- */

static void
repaint_file (WPanel * panel, int file_index, gboolean mv, int attr, gboolean isstatus)
{
    Widget *w = WIDGET (panel);

    int nth_column = 0;
    int width;
    int offset = 0;
    filename_scroll_flag_t ret_frm;
    int ypos = 0;
    gboolean panel_is_split;
    int fln = 0;

    panel_is_split = !isstatus && panel->list_cols > 1;
    width = w->cols - 2;

    if (panel_is_split)
    {
        nth_column = (file_index - panel->top_file) / panel_lines (panel);
        width /= panel->list_cols;

        offset = width * nth_column;

        if (nth_column + 1 >= panel->list_cols)
            width = w->cols - offset - 2;
    }

    /* Nothing to paint */
    if (width <= 0)
        return;

    if (mv)
    {
        ypos = file_index - panel->top_file;

        if (panel_is_split)
            ypos %= panel_lines (panel);

        ypos += 2;              /* top frame and header */
#ifdef WITH_TABS
        ypos += TABS_UP (panel) ? TABS_UP_VLINES : 0;
#endif
        widget_gotoyx (w, ypos, offset + 1);
    }

    ret_frm = format_file (panel, file_index, width, attr, isstatus, &fln);

    if (panel_is_split && nth_column + 1 < panel->list_cols)
    {
        tty_setcolor (NORMAL_COLOR);
        tty_print_one_vline (TRUE);
    }

    if (ret_frm != FILENAME_NOSCROLL && mv)
    {
        if (!panel_is_split && fln > 0)
        {
            if (panel->list_format != list_long)
                width = fln;
            else
            {
                offset = width - fln + 1;
                width = fln - 1;
            }
        }

        widget_gotoyx (w, ypos, offset);
        tty_setcolor (NORMAL_COLOR);
        tty_print_string (panel_filename_scroll_left_char);

        if ((ret_frm & FILENAME_SCROLL_RIGHT) != 0)
        {
            offset += width;
            if (nth_column + 1 >= panel->list_cols)
                offset++;

            widget_gotoyx (w, ypos, offset);
            tty_setcolor (NORMAL_COLOR);
            tty_print_string (panel_filename_scroll_right_char);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
display_mini_info (WPanel * panel)
{
    Widget *w = WIDGET (panel);

    if (!panels_options.show_mini_info)
        return;

#ifdef WITH_TABS
    widget_gotoyx (w, panel_lines (panel) + 3 + (TABS_UP (panel) ? TABS_UP_VLINES : 0), 1);
#else
    widget_gotoyx (w, panel_lines (panel) + 3, 1);
#endif

    if (panel->searching)
    {
        tty_setcolor (INPUT_COLOR);
        tty_print_char ('/');
        tty_print_string (str_fit_to_term (panel->search_buffer, w->cols - 3, J_LEFT));
        return;
    }

    /* Status resolves links and show them */
    set_colors (panel);

    if (S_ISLNK (panel->dir.list[panel->selected].st.st_mode))
    {
        char link_target[MC_MAXPATHLEN];
        vfs_path_t *lc_link_vpath;
        int len;

        lc_link_vpath =
            vfs_path_append_new (panel->cwd_vpath, panel->dir.list[panel->selected].fname,
                                 (char *) NULL);
        len = mc_readlink (lc_link_vpath, link_target, MC_MAXPATHLEN - 1);
        vfs_path_free (lc_link_vpath);
        if (len > 0)
        {
            link_target[len] = 0;
            tty_print_string ("-> ");
            tty_print_string (str_fit_to_term (link_target, w->cols - 5, J_LEFT_FIT));
        }
        else
            tty_print_string (str_fit_to_term (_("<readlink failed>"), w->cols - 2, J_LEFT));
    }
    else if (DIR_IS_DOTDOT (panel->dir.list[panel->selected].fname))
    {
        /* FIXME:
         * while loading directory (dir_list_load() and dir_list_reload()),
         * the actual stat info about ".." directory isn't got;
         * so just don't display incorrect info about ".." directory */
        tty_print_string (str_fit_to_term (_("UP--DIR"), w->cols - 2, J_LEFT));
    }
    else
        /* Default behavior */
        repaint_file (panel, panel->selected, FALSE, STATUS, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
paint_dir (WPanel * panel)
{
    int i;
    int items;                  /* Number of items */

    items = panel_items (panel);
    /* reset max len of filename because we have the new max length for the new file list */
    panel->max_shift = -1;

    for (i = 0; i < items; i++)
    {
        int color = 0;          /* Color value of the line */

        if (i + panel->top_file < panel->dir.len)
        {
            color = 2 * (panel->dir.list[i + panel->top_file].f.marked);
            color += (panel->selected == i + panel->top_file && panel->active);
        }

        repaint_file (panel, i + panel->top_file, TRUE, color, FALSE);
    }

    tty_set_normal_attrs ();
}

/* --------------------------------------------------------------------------------------------- */

static void
display_total_marked_size (const WPanel * panel, int y, int x, gboolean size_only)
{
    const Widget *w = CONST_WIDGET (panel);

    char buffer[BUF_SMALL], b_bytes[BUF_SMALL];
    const char *buf;
    int cols;

    if (panel->marked <= 0)
        return;

    buf = size_only ? b_bytes : buffer;
    cols = w->cols - 2;

    g_strlcpy (b_bytes, size_trunc_sep (panel->total, panels_options.kilobyte_si),
               sizeof (b_bytes));

    if (!size_only)
        g_snprintf (buffer, sizeof (buffer),
                    ngettext ("%s in %d file", "%s in %d files", panel->marked),
                    b_bytes, panel->marked);

    /* don't forget spaces around buffer content */
    buf = str_trunc (buf, cols - 4);

    if (x < 0)
        /* center in panel */
        x = (w->cols - str_term_width1 (buf)) / 2 - 1;

    /*
     * y == panel_lines (panel) + 2  for mini_info_separator
     * y == w->lines - 1             for panel bottom frame
     */
    widget_gotoyx (w, y, x);
    tty_setcolor (MARKED_COLOR);
    tty_printf (" %s ", buf);
}

/* --------------------------------------------------------------------------------------------- */

static void
mini_info_separator (const WPanel * panel)
{
    if (panels_options.show_mini_info)
    {
        const Widget *w = CONST_WIDGET (panel);
        int y;
#ifdef WITH_TABS
        y = panel_lines (panel) + 2 + (TABS_UP (panel) ? TABS_UP_VLINES : 0);
#else
        y = panel_lines (panel) + 2;
#endif

        tty_setcolor (NORMAL_COLOR);
        tty_draw_hline (w->y + y, w->x + 1, ACS_HLINE, w->cols - 2);
        /* Status displays total marked size.
         * Centered in panel, full format. */
        display_total_marked_size (panel, y, -1, FALSE);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
show_free_space (const WPanel * panel)
{
    /* Used to figure out how many free space we have */
    static struct my_statfs myfs_stats;
    /* Old current working directory for displaying free space */
    static char *old_cwd = NULL;

    /* Don't try to stat non-local fs */
    if (!vfs_file_is_local (panel->cwd_vpath) || !free_space)
        return;

    if (old_cwd == NULL || strcmp (old_cwd, vfs_path_as_str (panel->cwd_vpath)) != 0)
    {
        char rpath[PATH_MAX];

        init_my_statfs ();
        g_free (old_cwd);
        old_cwd = g_strdup (vfs_path_as_str (panel->cwd_vpath));

        if (mc_realpath (old_cwd, rpath) == NULL)
            return;

        my_statfs (&myfs_stats, rpath);
    }

    if (myfs_stats.avail != 0 || myfs_stats.total != 0)
    {
        const Widget *w = CONST_WIDGET (panel);
        char buffer1[6], buffer2[6], tmp[BUF_SMALL];

        size_trunc_len (buffer1, sizeof (buffer1) - 1, myfs_stats.avail, 1,
                        panels_options.kilobyte_si);
        size_trunc_len (buffer2, sizeof (buffer2) - 1, myfs_stats.total, 1,
                        panels_options.kilobyte_si);
        g_snprintf (tmp, sizeof (tmp), " %s/%s (%d%%) ", buffer1, buffer2,
                    myfs_stats.total == 0 ? 0 :
                    (int) (100 * (long double) myfs_stats.avail / myfs_stats.total));
        widget_gotoyx (w, w->lines - 1, w->cols - 2 - (int) strlen (tmp));
        tty_setcolor (NORMAL_COLOR);
        tty_print_string (tmp);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Prepare path string for showing in panel's header.
 * Passwords will removed, also home dir will replaced by ~
 *
 * @param panel WPanel object
 *
 * @return newly allocated string.
 */

static char *
panel_correct_path_to_show (const WPanel * panel)
{
    vfs_path_t *last_vpath;
    const vfs_path_element_t *path_element;
    char *return_path;
    int elements_count;

    elements_count = vfs_path_elements_count (panel->cwd_vpath);

    /* get last path element */
    path_element = vfs_path_element_clone (vfs_path_get_by_index (panel->cwd_vpath, -1));

    if (elements_count > 1 && (strcmp (path_element->class->name, "cpiofs") == 0 ||
                               strcmp (path_element->class->name, "extfs") == 0 ||
                               strcmp (path_element->class->name, "tarfs") == 0))
    {
        const char *archive_name;
        const vfs_path_element_t *prev_path_element;

        /* get previous path element for catching archive name */
        prev_path_element = vfs_path_get_by_index (panel->cwd_vpath, -2);
        archive_name = strrchr (prev_path_element->path, PATH_SEP);
        if (archive_name != NULL)
            last_vpath = vfs_path_from_str_flags (archive_name + 1, VPF_NO_CANON);
        else
        {
            last_vpath = vfs_path_from_str_flags (prev_path_element->path, VPF_NO_CANON);
            last_vpath->relative = TRUE;
        }
    }
    else
    {
        last_vpath = vfs_path_new ();
        last_vpath->relative = TRUE;
    }

    vfs_path_add_element (last_vpath, path_element);
    return_path =
        vfs_path_to_str_flags (last_vpath, 0,
                               VPF_STRIP_HOME | VPF_STRIP_PASSWORD | VPF_HIDE_CHARSET);
    vfs_path_free (last_vpath);

    return return_path;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Get Current path element encoding
 *
 * @param panel WPanel object
 *
 * @return newly allocated string or NULL if path charset is same as system charset
 */

#ifdef HAVE_CHARSET
static char *
panel_get_encoding_info_str (const WPanel * panel)
{
    char *ret_str = NULL;
    const vfs_path_element_t *path_element;

    path_element = vfs_path_get_by_index (panel->cwd_vpath, -1);
    if (path_element->encoding != NULL)
        ret_str = g_strdup_printf ("[%s]", path_element->encoding);

    return ret_str;
}
#endif

/* --------------------------------------------------------------------------------------------- */

static void
show_dir (const WPanel * panel)
{
    const Widget *w = CONST_WIDGET (panel);
    gchar *tmp;

    set_colors (panel);
    tty_draw_box (w->y, w->x, w->lines, w->cols, FALSE);

    if (panels_options.show_mini_info)
    {
        int y;

#ifdef WITH_TABS
        y = panel_lines (panel) + 2 + (TABS_UP (panel) ? TABS_UP_VLINES : 0);
#else
        y = panel_lines (panel) + 2;
#endif
        widget_gotoyx (w, y, 0);
        tty_print_alt_char (ACS_LTEE, FALSE);
        widget_gotoyx (w, y, w->cols - 1);
        tty_print_alt_char (ACS_RTEE, FALSE);
    }
    log4c(log4ccat, LOG4C_PRIORITY_INFO, "PanelLines %d ; TabsVisible: %d ; TabsUp: %d",CONST_WIDGET (panel)->lines, TABS_VISIBLE(panel), TABS_UP(panel));
    widget_gotoyx (w, 0, 1);
    tty_print_string (panel_history_prev_item_char);

    tmp = panels_options.show_dot_files ? panel_hiddenfiles_show_char : panel_hiddenfiles_hide_char;
    tmp = g_strdup_printf ("%s[%s]%s", tmp, panel_history_show_list_char,
                           panel_history_next_item_char);

    widget_gotoyx (w, 0, w->cols - 6);
    tty_print_string (tmp);

    g_free (tmp);

    widget_gotoyx (w, 0, 3);

    if (panel->is_panelized)
        tty_printf (" %s ", _("Panelize"));
#ifdef HAVE_CHARSET
    else
    {
        tmp = panel_get_encoding_info_str (panel);
        if (tmp != NULL)
        {
            tty_printf ("%s", tmp);
            widget_gotoyx (w, 0, 3 + strlen (tmp));
            g_free (tmp);
        }
    }
#endif

    if (panel->active)
        tty_setcolor (REVERSE_COLOR);

    tmp = panel_correct_path_to_show (panel);
    tty_printf (" %s ", str_term_trim (tmp, MIN (MAX (w->cols - 12, 0), w->cols)));
    g_free (tmp);

    if (!panels_options.show_mini_info)
    {
        if (panel->marked == 0)
        {
            /* Show size of curret file in the bottom of panel */
            if (S_ISREG (panel->dir.list[panel->selected].st.st_mode))
            {
                char buffer[BUF_SMALL];

                g_snprintf (buffer, sizeof (buffer), " %s ",
                            size_trunc_sep (panel->dir.list[panel->selected].st.st_size,
                                            panels_options.kilobyte_si));
                tty_setcolor (NORMAL_COLOR);
                widget_gotoyx (w, w->lines - 1, 4);
                tty_print_string (buffer);
            }
        }
        else
        {
            /* Show total size of marked files
             * In the bottom of panel, display size only. */
            display_total_marked_size (panel, w->lines - 1, 2, TRUE);
        }
    }

    show_free_space (panel);

    if (panel->active)
        tty_set_normal_attrs ();
}

/* --------------------------------------------------------------------------------------------- */

static void
adjust_top_file (WPanel * panel)
{
    int items;

    /* Update panel->selected to avoid out of range in panel->dir.list[panel->selected]
     * when panel is redrawing when directory is reloading, for example in path:
     * dir_list_reload() -> mc_refresh() -> dialog_change_screen_size() ->
     * midnight_callback (MSG_RESIZE) -> setup_panels() -> panel_callback(MSG_DRAW) ->
     * display_mini_info()
     */
    panel->selected = CLAMP (panel->selected, 0, panel->dir.len - 1);

    items = panel_items (panel);

    if (panel->dir.len <= items)
    {
        /* If all files fit, show them all. */
        panel->top_file = 0;
    }
    else
    {
        int i;

        /* top_file has to be in the range [selected-items+1, selected] so that
           the selected file is visible.
           top_file should be in the range [0, count-items] so that there's
           no empty space wasted.
           Within these ranges, adjust it by as little as possible. */

        if (panel->top_file < 0)
            panel->top_file = 0;

        i = panel->selected - items + 1;
        if (panel->top_file < i)
            panel->top_file = i;

        i = panel->dir.len - items;
        if (panel->top_file > i)
            panel->top_file = i;

        if (panel->top_file > panel->selected)
            panel->top_file = panel->selected;
    }
}

/* --------------------------------------------------------------------------------------------- */
/** add "#enc:encodning" to end of path */
/* if path end width a previous #enc:, only encoding is changed no additional
 * #enc: is appended
 * retun new string
 */

static char *
panel_save_name (WPanel * panel)
{
    /* If the program is shuting down */
    if ((mc_global.midnight_shutdown && auto_save_setup) || saving_setup)
        return g_strdup (panel->panel_name);

    return g_strconcat ("Temporal:", panel->panel_name, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
directory_history_add (WPanel * panel, const vfs_path_t * vpath)
{
    char *tmp;

    tmp = vfs_path_to_str_flags (vpath, 0, VPF_STRIP_PASSWORD);
    panel->dir_history = list_append_unique (panel->dir_history, tmp);
    panel->dir_history_current = panel->dir_history;
}

/* --------------------------------------------------------------------------------------------- */

/* "history_load" event handler */
static gboolean
panel_load_history (const gchar * event_group_name, const gchar * event_name,
                    gpointer init_data, gpointer data)
{
    WPanel *p = PANEL (init_data);
    ev_history_load_save_t *ev = (ev_history_load_save_t *) data;

    (void) event_group_name;
    (void) event_name;

    if (ev->receiver == NULL || ev->receiver == WIDGET (p))
    {
        if (ev->cfg != NULL)
            p->dir_history = mc_config_history_load (ev->cfg, p->hist_name);
        else
            p->dir_history = mc_config_history_get (p->hist_name);

        directory_history_add (p, p->cwd_vpath);
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* "history_save" event handler */
static gboolean
panel_save_history (const gchar * event_group_name, const gchar * event_name,
                    gpointer init_data, gpointer data)
{
    WPanel *p = PANEL (init_data);

    (void) event_group_name;
    (void) event_name;

    if (p->dir_history != NULL)
    {
        ev_history_load_save_t *ev = (ev_history_load_save_t *) data;

        mc_config_history_save (ev->cfg, p->hist_name, p->dir_history);
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_destroy (WPanel * p)
{
    size_t i;

#ifdef WITH_TABS
    destroy_tabs (p);
#endif
    if (panels_options.auto_save_setup)
    {
        char *name;

        name = panel_save_name (p);
        panel_save_setup (p, name);
        g_free (name);
    }

    panel_clean_dir (p);

    /* clean history */
    if (p->dir_history != NULL)
    {
        /* directory history is already saved before this moment */
        p->dir_history = g_list_first (p->dir_history);
        g_list_free_full (p->dir_history, g_free);
    }
    g_free (p->hist_name);

    g_slist_free_full (p->format, (GDestroyNotify) format_item_free);
    g_slist_free_full (p->status_format, (GDestroyNotify) format_item_free);

    g_free (p->user_format);
    for (i = 0; i < LIST_FORMATS; i++)
        g_free (p->user_status_format[i]);

    g_free (p->dir.list);
    g_free (p->panel_name);

    vfs_path_free (p->lwd_vpath);
    vfs_path_free (p->cwd_vpath);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_paint_sort_info (const WPanel * panel)
{
    if (*panel->sort_field->hotkey != '\0')
    {
        const char *sort_sign =
            panel->sort_info.reverse ? panel_sort_up_char : panel_sort_down_char;
        char *str;

        str = g_strdup_printf ("%s%s", sort_sign, Q_ (panel->sort_field->hotkey));
#ifdef WITH_TABS
        widget_gotoyx (panel, 1 + (TABS_UP (panel) ? TABS_UP_VLINES : 0), 1);
#else
        widget_gotoyx (panel, 1, 1);
#endif
        tty_print_string (str);
        g_free (str);
    }
}

/* --------------------------------------------------------------------------------------------- */

static const char *
panel_get_title_without_hotkey (const char *title)
{
    static char translated_title[BUF_TINY];

    if (title == NULL || title[0] == '\0')
        translated_title[0] = '\0';
    else
    {
        char *hkey;

        g_snprintf (translated_title, sizeof (translated_title), "%s", _(title));

        hkey = strchr (translated_title, '&');
        if (hkey != NULL && hkey[1] != '\0')
            memmove (hkey, hkey + 1, strlen (hkey));
    }

    return translated_title;
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_print_header (const WPanel * panel)
{
    const Widget *w = CONST_WIDGET (panel);

    int y, x;
    int i;
    GString *format_txt;

    widget_gotoyx (w, 4, 1);
    tty_getyx (&y, &x);
    tty_setcolor (NORMAL_COLOR);
    tty_draw_hline (y, x, ' ', w->cols - 2);

    format_txt = g_string_new ("");

    for (i = 0; i < panel->list_cols; i++)
    {
        GSList *format;

        for (format = panel->format; format != NULL; format = g_slist_next (format))
        {
            format_item_t *fi = (format_item_t *) format->data;

            if (fi->string_fn != NULL)
            {
                g_string_set_size (format_txt, 0);

                if (panel->list_format == list_long && strcmp (fi->id, panel->sort_field->id) == 0)
                    g_string_append (format_txt,
                                     panel->sort_info.reverse
                                     ? panel_sort_up_char : panel_sort_down_char);

                g_string_append (format_txt, fi->title);

                if (panel->filter != NULL && strcmp (fi->id, "name") == 0)
                {
                    g_string_append (format_txt, " [");
                    g_string_append (format_txt, panel->filter);
                    g_string_append (format_txt, "]");
                }

                tty_setcolor (HEADER_COLOR);
                tty_print_string (str_fit_to_term (format_txt->str, fi->field_len, J_CENTER_LEFT));
                log4c(log4ccat, LOG4C_PRIORITY_INFO, "str: %s",format_txt->str);
            }
            else
            {
                tty_setcolor (NORMAL_COLOR);
                tty_print_one_vline (TRUE);
            }
        }

        if (i < panel->list_cols - 1)
        {
            tty_setcolor (NORMAL_COLOR);
            tty_print_one_vline (TRUE);
        }
    }

    g_string_free (format_txt, TRUE);

    if (panel->list_format != list_long)
        panel_paint_sort_info (panel);
}

/* --------------------------------------------------------------------------------------------- */

static const char *
parse_panel_size (WPanel * panel, const char *format, gboolean isstatus)
{
    panel_display_t frame = frame_half;

    format = skip_separators (format);

    if (strncmp (format, "full", 4) == 0)
    {
        frame = frame_full;
        format += 4;
    }
    else if (strncmp (format, "half", 4) == 0)
    {
        frame = frame_half;
        format += 4;
    }

    if (!isstatus)
    {
        panel->frame_size = frame;
        panel->list_cols = 1;
    }

    /* Now, the optional column specifier */
    format = skip_separators (format);

    if (g_ascii_isdigit (*format))
    {
        if (!isstatus)
        {
            panel->list_cols = g_ascii_digit_value (*format);
            if (panel->list_cols < 1)
                panel->list_cols = 1;
        }

        format++;
    }

    if (!isstatus)
        panel_update_cols (WIDGET (panel), panel->frame_size);

    return skip_separators (format);
}

/* --------------------------------------------------------------------------------------------- */

/* *INDENT-OFF* */
/* Format is:

   all              := panel_format? format
   panel_format     := [full|half] [1-9]
   format           := one_format_item_t
                     | format , one_format_item_t

   one_format_item_t := just format.id [opt_size]
   just              := [<=>]
   opt_size          := : size [opt_expand]
   size              := [0-9]+
   opt_expand        := +

 */
/* *INDENT-ON* */

static GSList *
parse_display_format (WPanel * panel, const char *format, char **error, gboolean isstatus,
                      int *res_total_cols)
{
    GSList *home = NULL;        /* The formats we return */
    int total_cols = 0;         /* Used columns by the format */
    size_t i;

    static size_t i18n_timelength = 0;  /* flag: check ?Time length at startup */

    *error = NULL;

    if (i18n_timelength == 0)
    {
        i18n_timelength = i18n_checktimelength ();      /* Musn't be 0 */

        for (i = 0; panel_fields[i].id != NULL; i++)
            if (strcmp ("time", panel_fields[i].id + 1) == 0)
                panel_fields[i].min_size = i18n_timelength;
    }

    /*
     * This makes sure that the panel and mini status full/half mode
     * setting is equal
     */
    format = parse_panel_size (panel, format, isstatus);

    while (*format != '\0')
    {                           /* format can be an empty string */
        format_item_t *darr;
        align_crt_t justify;    /* Which mode. */
        gboolean set_justify = TRUE;    /* flag: set justification mode? */
        gboolean found = FALSE;
        size_t klen = 0;

        darr = g_new0 (format_item_t, 1);
        home = g_slist_append (home, darr);

        format = skip_separators (format);

        switch (*format)
        {
        case '<':
            justify = J_LEFT;
            format = skip_separators (format + 1);
            break;
        case '=':
            justify = J_CENTER;
            format = skip_separators (format + 1);
            break;
        case '>':
            justify = J_RIGHT;
            format = skip_separators (format + 1);
            break;
        default:
            justify = J_LEFT;
            set_justify = FALSE;
            break;
        }

        for (i = 0; !found && panel_fields[i].id != NULL; i++)
        {
            klen = strlen (panel_fields[i].id);
            found = strncmp (format, panel_fields[i].id, klen) == 0;
        }

        if (found)
        {
            i--;
            format += klen;

            darr->requested_field_len = panel_fields[i].min_size;
            darr->string_fn = panel_fields[i].string_fn;
            darr->title = g_strdup (panel_get_title_without_hotkey (panel_fields[i].title_hotkey));
            darr->id = panel_fields[i].id;
            darr->expand = panel_fields[i].expands;
            darr->just_mode = panel_fields[i].default_just;

            if (set_justify)
            {
                if (IS_FIT (darr->just_mode))
                    darr->just_mode = MAKE_FIT (justify);
                else
                    darr->just_mode = justify;
            }

            format = skip_separators (format);

            /* If we have a size specifier */
            if (*format == ':')
            {
                int req_length;

                /* If the size was specified, we don't want
                 * auto-expansion by default
                 */
                darr->expand = FALSE;
                format++;
                req_length = atoi (format);
                darr->requested_field_len = req_length;

                format = skip_numbers (format);

                /* Now, if they insist on expansion */
                if (*format == '+')
                {
                    darr->expand = TRUE;
                    format++;
                }
            }
        }
        else
        {
            size_t pos;
            char *tmp_format;

            pos = strlen (format);
            if (pos > 8)
                pos = 8;

            tmp_format = g_strndup (format, pos);
            g_slist_free_full (home, (GDestroyNotify) format_item_free);
            *error =
                g_strconcat (_("Unknown tag on display format:"), " ", tmp_format, (char *) NULL);
            g_free (tmp_format);

            return NULL;
        }

        total_cols += darr->requested_field_len;
    }

    *res_total_cols = total_cols;
    return home;
}

/* --------------------------------------------------------------------------------------------- */

static GSList *
use_display_format (WPanel * panel, const char *format, char **error, gboolean isstatus)
{
#define MAX_EXPAND 4
    int expand_top = 0;         /* Max used element in expand */
    int usable_columns;         /* Usable columns in the panel */
    int total_cols = 0;
    GSList *darr, *home;

    if (format == NULL)
        format = DEFAULT_USER_FORMAT;

    home = parse_display_format (panel, format, error, isstatus, &total_cols);

    if (*error != NULL)
        return NULL;

    panel->dirty = 1;

    usable_columns = WIDGET (panel)->cols - 2;
    /* Status needn't to be split */
    if (!isstatus)
    {
        usable_columns /= panel->list_cols;
        if (panel->list_cols > 1)
            usable_columns--;
    }

    /* Look for the expandable fields and set field_len based on the requested field len */
    for (darr = home; darr != NULL && expand_top < MAX_EXPAND; darr = g_slist_next (darr))
    {
        format_item_t *fi = (format_item_t *) darr->data;

        fi->field_len = fi->requested_field_len;
        if (fi->expand)
            expand_top++;
    }

    /* If we used more columns than the available columns, adjust that */
    if (total_cols > usable_columns)
    {
        int dif;
        int pdif = 0;

        dif = total_cols - usable_columns;

        while (dif != 0 && pdif != dif)
        {
            pdif = dif;

            for (darr = home; darr != NULL; darr = g_slist_next (darr))
            {
                format_item_t *fi = (format_item_t *) darr->data;

                if (dif != 0 && fi->field_len != 1)
                {
                    fi->field_len--;
                    dif--;
                }
            }
        }

        total_cols = usable_columns;    /* give up, the rest should be truncated */
    }

    /* Expand the available space */
    if (usable_columns > total_cols && expand_top != 0)
    {
        int i;
        int spaces;

        spaces = (usable_columns - total_cols) / expand_top;

        for (i = 0, darr = home; darr != NULL && i < expand_top; darr = g_slist_next (darr))
        {
            format_item_t *fi = (format_item_t *) darr->data;

            if (fi->expand)
            {
                fi->field_len += spaces;
                if (i == 0)
                    fi->field_len += (usable_columns - total_cols) % expand_top;
                i++;
            }
        }
    }

    return home;
}

/* --------------------------------------------------------------------------------------------- */
/** Given the panel->view_type returns the format string to be parsed */

static const char *
panel_format (WPanel * panel)
{
    switch (panel->list_format)
    {
    case list_long:
        return "full perm space nlink space owner space group space size space mtime space name";

    case list_brief:
        {
            static char format[BUF_TINY];
            int brief_cols = panel->brief_cols;

            if (brief_cols < 1)
                brief_cols = 2;

            if (brief_cols > 9)
                brief_cols = 9;

            g_snprintf (format, sizeof (format), "half %d type name", brief_cols);
            return format;
        }

    case list_user:
        return panel->user_format;

    default:
    case list_full:
        return "half type name | size | mtime";
    }
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mini_status_format (WPanel * panel)
{
    if (panel->user_mini_status)
        return panel->user_status_format[panel->list_format];

    switch (panel->list_format)
    {
    case list_long:
        return "full perm space nlink space owner space group space size space mtime space name";

    case list_brief:
        return "half type name space bsize space perm space";

    case list_full:
        return "half type name";

    default:
    case list_user:
        return panel->user_format;
    }
}

/*                          */
/* Panel operation commands */
/*                          */

/* --------------------------------------------------------------------------------------------- */

static void
cd_up_dir (void)
{
    vfs_path_t *up_dir;

    up_dir = vfs_path_from_str ("..");
    do_cd (up_dir, cd_exact);
    vfs_path_free (up_dir);
}

/* --------------------------------------------------------------------------------------------- */
/** Used to emulate Lynx's entering leaving a directory with the arrow keys */

static cb_ret_t
maybe_cd (gboolean move_up_dir)
{
    if (panels_options.navigate_with_arrows && input_is_empty (cmdline))
    {
        if (move_up_dir)
        {
            cd_up_dir ();
            return MSG_HANDLED;
        }

        if (S_ISDIR (selection (current_panel)->st.st_mode)
            || link_isdir (selection (current_panel)))
        {
            vfs_path_t *vpath;

            vpath = vfs_path_from_str (selection (current_panel)->fname);
            do_cd (vpath, cd_exact);
            vfs_path_free (vpath);
            return MSG_HANDLED;
        }
    }

    return MSG_NOT_HANDLED;
}

/* --------------------------------------------------------------------------------------------- */

/* if command line is empty then do 'cd ..' */
static cb_ret_t
force_maybe_cd (void)
{
    if (input_is_empty (cmdline))
    {
        cd_up_dir ();
        return MSG_HANDLED;
    }

    return MSG_NOT_HANDLED;
}

/* --------------------------------------------------------------------------------------------- */

static inline void
unselect_item (WPanel * panel)
{
    repaint_file (panel, panel->selected, TRUE, 2 * selection (panel)->f.marked, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
/** Select/unselect all the files like a current file by extension */

static void
panel_select_ext_cmd (void)
{
    char *filename;
    gboolean do_select;
    char *reg_exp, *cur_file_ext;
    mc_search_t *search;
    int i;

    filename = selection (current_panel)->fname;
    if (filename == NULL)
        return;

    do_select = !selection (current_panel)->f.marked;

    cur_file_ext = strutils_regex_escape (extension (filename));
    if (cur_file_ext[0] != '\0')
        reg_exp = g_strconcat ("^.*\\.", cur_file_ext, "$", (char *) NULL);
    else
        reg_exp = g_strdup ("^[^\\.]+$");

    g_free (cur_file_ext);

    search = mc_search_new (reg_exp, NULL);
    search->search_type = MC_SEARCH_T_REGEX;
    search->is_case_sensitive = FALSE;

    for (i = 0; i < current_panel->dir.len; i++)
    {
        file_entry_t *file_entry = &current_panel->dir.list[i];

        if (DIR_IS_DOTDOT (file_entry->fname) || S_ISDIR (file_entry->st.st_mode))
            continue;

        if (!mc_search_run (search, file_entry->fname, 0, file_entry->fnamelen, NULL))
            continue;

        do_file_mark (current_panel, i, do_select ? 1 : 0);
    }

    mc_search_free (search);
    g_free (reg_exp);
}

/* --------------------------------------------------------------------------------------------- */

static int
panel_selected_at_half (const WPanel * panel)
{
    int lines, top;

    lines = panel_lines (panel);

    /* define top file of column */
    top = panel->top_file;
    if (panel->list_cols > 1)
        top += lines * ((panel->selected - top) / lines);

    return (panel->selected - top - lines / 2);
}

/* --------------------------------------------------------------------------------------------- */

static void
move_down (WPanel * panel)
{
    int items;

    if (panel->selected + 1 == panel->dir.len)
        return;

    unselect_item (panel);
    panel->selected++;

    items = panel_items (panel);

    if (panels_options.scroll_pages && panel->selected - panel->top_file == items)
    {
        /* Scroll window half screen */
        panel->top_file += items / 2;
        if (panel->top_file > panel->dir.len - items)
            panel->top_file = panel->dir.len - items;
        paint_dir (panel);
    }
    else if (panels_options.scroll_center && panel_selected_at_half (panel) > 0)
    {
        /* Scroll window when cursor is halfway down */
        panel->top_file++;
        if (panel->top_file > panel->dir.len - items)
            panel->top_file = panel->dir.len - items;
    }
    select_item (panel);
}

/* --------------------------------------------------------------------------------------------- */

static void
move_up (WPanel * panel)
{
    if (panel->selected == 0)
        return;

    unselect_item (panel);
    panel->selected--;

    if (panels_options.scroll_pages && panel->selected < panel->top_file)
    {
        /* Scroll window half screen */
        panel->top_file -= panel_items (panel) / 2;
        if (panel->top_file < 0)
            panel->top_file = 0;
        paint_dir (panel);
    }
    else if (panels_options.scroll_center && panel_selected_at_half (panel) < 0)
    {
        /* Scroll window when cursor is halfway up */
        panel->top_file--;
        if (panel->top_file < 0)
            panel->top_file = 0;
    }
    select_item (panel);
}

/* --------------------------------------------------------------------------------------------- */
/** Changes the selection by lines (may be negative) */

static void
move_selection (WPanel * panel, int lines)
{
    int new_pos;
    gboolean adjust = FALSE;

    new_pos = panel->selected + lines;
    if (new_pos >= panel->dir.len)
        new_pos = panel->dir.len - 1;

    if (new_pos < 0)
        new_pos = 0;

    unselect_item (panel);
    panel->selected = new_pos;

    if (panel->selected - panel->top_file >= panel_items (panel))
    {
        panel->top_file += lines;
        adjust = TRUE;
    }

    if (panel->selected - panel->top_file < 0)
    {
        panel->top_file += lines;
        adjust = TRUE;
    }

    if (adjust)
    {
        if (panel->top_file > panel->selected)
            panel->top_file = panel->selected;
        if (panel->top_file < 0)
            panel->top_file = 0;
        paint_dir (panel);
    }
    select_item (panel);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
move_left (WPanel * panel)
{
    if (panel->list_cols > 1)
    {
        move_selection (panel, -panel_lines (panel));
        return MSG_HANDLED;
    }

    return maybe_cd (TRUE);     /* cd .. */
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
move_right (WPanel * panel)
{
    if (panel->list_cols > 1)
    {
        move_selection (panel, panel_lines (panel));
        return MSG_HANDLED;
    }

    return maybe_cd (FALSE);    /* cd (selection) */
}

/* --------------------------------------------------------------------------------------------- */

static void
prev_page (WPanel * panel)
{
    int items;

    if (panel->selected == 0 && panel->top_file == 0)
        return;

    unselect_item (panel);
    items = panel_items (panel);
    if (panel->top_file < items)
        items = panel->top_file;
    if (items == 0)
        panel->selected = 0;
    else
        panel->selected -= items;
    panel->top_file -= items;

    select_item (panel);
    paint_dir (panel);
}

/* --------------------------------------------------------------------------------------------- */

static void
goto_parent_dir (WPanel * panel)
{
    if (!panel->is_panelized)
        cd_up_dir ();
    else
    {
        char *fname = panel->dir.list[panel->selected].fname;
        const char *bname;
        vfs_path_t *dname_vpath;

        if (g_path_is_absolute (fname))
            fname = g_strdup (fname);
        else
            fname =
                mc_build_filename (vfs_path_as_str (panelized_panel.root_vpath), fname,
                                   (char *) NULL);

        bname = x_basename (fname);

        if (bname == fname)
            dname_vpath = vfs_path_from_str (".");
        else
        {
            char *dname;

            dname = g_strndup (fname, bname - fname);
            dname_vpath = vfs_path_from_str (dname);
            g_free (dname);
        }

        do_cd (dname_vpath, cd_exact);
        try_to_select (panel, bname);

        vfs_path_free (dname_vpath);
        g_free (fname);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
next_page (WPanel * panel)
{
    int items;

    if (panel->selected == panel->dir.len - 1)
        return;

    unselect_item (panel);
    items = panel_items (panel);
    if (panel->top_file > panel->dir.len - 2 * items)
        items = panel->dir.len - items - panel->top_file;
    if (panel->top_file + items < 0)
        items = -panel->top_file;
    if (items == 0)
        panel->selected = panel->dir.len - 1;
    else
        panel->selected += items;
    panel->top_file += items;

    select_item (panel);
    paint_dir (panel);
}

/* --------------------------------------------------------------------------------------------- */

static void
goto_child_dir (WPanel * panel)
{
    if ((S_ISDIR (selection (panel)->st.st_mode) || link_isdir (selection (panel))))
    {
        vfs_path_t *vpath;

        vpath = vfs_path_from_str (selection (panel)->fname);
        do_cd (vpath, cd_exact);
        vfs_path_free (vpath);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
goto_top_file (WPanel * panel)
{
    unselect_item (panel);
    panel->selected = panel->top_file;
    select_item (panel);
}

/* --------------------------------------------------------------------------------------------- */

static void
goto_middle_file (WPanel * panel)
{
    unselect_item (panel);
    panel->selected = panel->top_file + panel_items (panel) / 2;
    select_item (panel);
}

/* --------------------------------------------------------------------------------------------- */

static void
goto_bottom_file (WPanel * panel)
{
    unselect_item (panel);
    panel->selected = panel->top_file + panel_items (panel) - 1;
    select_item (panel);
}

/* --------------------------------------------------------------------------------------------- */

static void
move_home (WPanel * panel)
{
    if (panel->selected == 0)
        return;

    unselect_item (panel);

    if (panels_options.torben_fj_mode)
    {
        int middle_pos;

        middle_pos = panel->top_file + panel_items (panel) / 2;

        if (panel->selected > middle_pos)
        {
            goto_middle_file (panel);
            return;
        }
        if (panel->selected != panel->top_file)
        {
            goto_top_file (panel);
            return;
        }
    }

    panel->top_file = 0;
    panel->selected = 0;

    paint_dir (panel);
    select_item (panel);
}

/* --------------------------------------------------------------------------------------------- */

static void
move_end (WPanel * panel)
{
    if (panel->selected == panel->dir.len - 1)
        return;

    unselect_item (panel);

    if (panels_options.torben_fj_mode)
    {
        int items, middle_pos;

        items = panel_items (panel);
        middle_pos = panel->top_file + items / 2;

        if (panel->selected < middle_pos)
        {
            goto_middle_file (panel);
            return;
        }
        if (panel->selected != panel->top_file + items - 1)
        {
            goto_bottom_file (panel);
            return;
        }
    }

    panel->selected = panel->dir.len - 1;
    paint_dir (panel);
    select_item (panel);
}

/* --------------------------------------------------------------------------------------------- */

static void
do_mark_file (WPanel * panel, mark_act_t do_move)
{
    do_file_mark (panel, panel->selected, selection (panel)->f.marked ? 0 : 1);

    if ((panels_options.mark_moves_down && do_move == MARK_DOWN) || do_move == MARK_FORCE_DOWN)
        move_down (panel);
    else if (do_move == MARK_FORCE_UP)
        move_up (panel);
}

/* --------------------------------------------------------------------------------------------- */

static inline void
mark_file (WPanel * panel)
{
    do_mark_file (panel, MARK_DOWN);
}

/* --------------------------------------------------------------------------------------------- */

static inline void
mark_file_up (WPanel * panel)
{
    do_mark_file (panel, MARK_FORCE_UP);
}

/* --------------------------------------------------------------------------------------------- */

static inline void
mark_file_down (WPanel * panel)
{
    do_mark_file (panel, MARK_FORCE_DOWN);
}

/* --------------------------------------------------------------------------------------------- */

static void
mark_file_right (WPanel * panel)
{
    int lines;

    if (state_mark < 0)
        state_mark = selection (panel)->f.marked ? 0 : 1;

    lines = panel_lines (panel);
    lines = MIN (lines, panel->dir.len - panel->selected - 1);
    for (; lines != 0; lines--)
    {
        do_file_mark (panel, panel->selected, state_mark);
        move_down (panel);
    }
    do_file_mark (panel, panel->selected, state_mark);
}

/* --------------------------------------------------------------------------------------------- */

static void
mark_file_left (WPanel * panel)
{
    int lines;

    if (state_mark < 0)
        state_mark = selection (panel)->f.marked ? 0 : 1;

    lines = panel_lines (panel);
    lines = MIN (lines, panel->selected + 1);
    for (; lines != 0; lines--)
    {
        do_file_mark (panel, panel->selected, state_mark);
        move_up (panel);
    }
    do_file_mark (panel, panel->selected, state_mark);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_select_unselect_files (WPanel * panel, const char *title, const char *history_name,
                             gboolean do_select)
{
    gboolean files_only = (panels_options.select_flags & SELECT_FILES_ONLY) != 0;
    gboolean case_sens = (panels_options.select_flags & SELECT_MATCH_CASE) != 0;
    gboolean shell_patterns = (panels_options.select_flags & SELECT_SHELL_PATTERNS) != 0;

    char *reg_exp;
    mc_search_t *search;
    int i;

    quick_widget_t quick_widgets[] = {
        /* *INDENT-OFF* */
        QUICK_INPUT (INPUT_LAST_TEXT, history_name, &reg_exp, NULL,
                     FALSE, FALSE, INPUT_COMPLETE_FILENAMES),
        QUICK_START_COLUMNS,
            QUICK_CHECKBOX (N_("&Files only"), &files_only, NULL),
            QUICK_CHECKBOX (N_("&Using shell patterns"), &shell_patterns, NULL),
        QUICK_NEXT_COLUMN,
            QUICK_CHECKBOX (N_("&Case sensitive"), &case_sens, NULL),
        QUICK_STOP_COLUMNS,
        QUICK_END
        /* *INDENT-ON* */
    };

    quick_dialog_t qdlg = {
        -1, -1, 50,
        title, "[Select/Unselect Files]",
        quick_widgets, NULL, NULL
    };

    if (quick_dialog (&qdlg) == B_CANCEL)
        return;

    if (reg_exp == NULL || *reg_exp == '\0')
    {
        g_free (reg_exp);
        return;
    }

    search = mc_search_new (reg_exp, NULL);
    search->search_type = shell_patterns ? MC_SEARCH_T_GLOB : MC_SEARCH_T_REGEX;
    search->is_entire_line = TRUE;
    search->is_case_sensitive = case_sens;

    for (i = 0; i < panel->dir.len; i++)
    {
        if (DIR_IS_DOTDOT (panel->dir.list[i].fname))
            continue;
        if (S_ISDIR (panel->dir.list[i].st.st_mode) && files_only)
            continue;

        if (mc_search_run (search, panel->dir.list[i].fname, 0, panel->dir.list[i].fnamelen, NULL))
            do_file_mark (panel, i, do_select ? 1 : 0);
    }

    mc_search_free (search);
    g_free (reg_exp);

    /* result flags */
    panels_options.select_flags = 0;
    if (case_sens)
        panels_options.select_flags |= SELECT_MATCH_CASE;
    if (files_only)
        panels_options.select_flags |= SELECT_FILES_ONLY;
    if (shell_patterns)
        panels_options.select_flags |= SELECT_SHELL_PATTERNS;
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_select_files (WPanel * panel)
{
    panel_select_unselect_files (panel, _("Select"), ":select_cmd: Select ", TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_unselect_files (WPanel * panel)
{
    panel_select_unselect_files (panel, _("Unselect"), ":unselect_cmd: Unselect ", FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_select_invert_files (WPanel * panel)
{
    int i;

    for (i = 0; i < panel->dir.len; i++)
    {
        file_entry_t *file = &panel->dir.list[i];

        if (!panels_options.reverse_files_only || !S_ISDIR (file->st.st_mode))
            do_file_mark (panel, i, file->f.marked ? 0 : 1);
    }
}

/* --------------------------------------------------------------------------------------------- */
/** Incremental search of a file name in the panel.
  * @param panel instance of WPanel structure
  * @param c_code key code
  */

static void
do_search (WPanel * panel, int c_code)
{
    size_t l;
    int i, sel;
    gboolean wrapped = FALSE;
    char *act;
    mc_search_t *search;
    char *reg_exp, *esc_str;
    gboolean is_found = FALSE;

    l = strlen (panel->search_buffer);
    if (c_code == KEY_BACKSPACE)
    {
        if (l != 0)
        {
            act = panel->search_buffer + l;
            str_prev_noncomb_char (&act, panel->search_buffer);
            act[0] = '\0';
        }
        panel->search_chpoint = 0;
    }
    else
    {
        if (c_code != 0 && (gsize) panel->search_chpoint < sizeof (panel->search_char))
        {
            panel->search_char[panel->search_chpoint] = c_code;
            panel->search_chpoint++;
        }

        if (panel->search_chpoint > 0)
        {
            switch (str_is_valid_char (panel->search_char, panel->search_chpoint))
            {
            case -2:
                return;
            case -1:
                panel->search_chpoint = 0;
                return;
            default:
                if (l + panel->search_chpoint < sizeof (panel->search_buffer))
                {
                    memcpy (panel->search_buffer + l, panel->search_char, panel->search_chpoint);
                    l += panel->search_chpoint;
                    panel->search_buffer[l] = '\0';
                    panel->search_chpoint = 0;
                }
            }
        }
    }

    reg_exp = g_strdup_printf ("%s*", panel->search_buffer);
    esc_str = strutils_escape (reg_exp, -1, ",|\\{}[]", TRUE);
    search = mc_search_new (esc_str, NULL);
    search->search_type = MC_SEARCH_T_GLOB;
    search->is_entire_line = TRUE;

    switch (panels_options.qsearch_mode)
    {
    case QSEARCH_CASE_SENSITIVE:
        search->is_case_sensitive = TRUE;
        break;
    case QSEARCH_CASE_INSENSITIVE:
        search->is_case_sensitive = FALSE;
        break;
    default:
        search->is_case_sensitive = panel->sort_info.case_sensitive;
        break;
    }

    sel = panel->selected;

    for (i = panel->selected; !wrapped || i != panel->selected; i++)
    {
        if (i >= panel->dir.len)
        {
            i = 0;
            if (wrapped)
                break;
            wrapped = TRUE;
        }
        if (mc_search_run (search, panel->dir.list[i].fname, 0, panel->dir.list[i].fnamelen, NULL))
        {
            sel = i;
            is_found = TRUE;
            break;
        }
    }
    if (is_found)
    {
        unselect_item (panel);
        panel->selected = sel;
        select_item (panel);
        widget_draw (WIDGET (panel));
    }
    else if (c_code != KEY_BACKSPACE)
    {
        act = panel->search_buffer + l;
        str_prev_noncomb_char (&act, panel->search_buffer);
        act[0] = '\0';
    }
    mc_search_free (search);
    g_free (reg_exp);
    g_free (esc_str);
}

/* --------------------------------------------------------------------------------------------- */
/** Start new search.
  * @param panel instance of WPanel structure
  */

static void
start_search (WPanel * panel)
{
    if (panel->searching)
    {
        if (panel->selected == panel->dir.len - 1)
            panel->selected = 0;
        else
            move_down (panel);

        /* in case if there was no search string we need to recall
           previous string, with which we ended previous searching */
        if (panel->search_buffer[0] == '\0')
            g_strlcpy (panel->search_buffer, panel->prev_search_buffer,
                       sizeof (panel->search_buffer));

        do_search (panel, 0);
    }
    else
    {
        panel->searching = TRUE;
        panel->search_buffer[0] = '\0';
        panel->search_char[0] = '\0';
        panel->search_chpoint = 0;
        display_mini_info (panel);
        mc_refresh ();
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
stop_search (WPanel * panel)
{
    panel->searching = FALSE;

    /* if user had overrdied search string, we need to store it
       to the previous_search_buffer */
    if (panel->search_buffer[0] != '\0')
        g_strlcpy (panel->prev_search_buffer, panel->search_buffer,
                   sizeof (panel->prev_search_buffer));

    display_mini_info (panel);
}

/* --------------------------------------------------------------------------------------------- */
/** Return TRUE if the Enter key has been processed, FALSE otherwise */

static gboolean
do_enter_on_file_entry (file_entry_t * fe)
{
    vfs_path_t *full_name_vpath;
    gboolean ok;

    /*
     * Directory or link to directory - change directory.
     * Try the same for the entries on which mc_lstat() has failed.
     */
    if (S_ISDIR (fe->st.st_mode) || link_isdir (fe) || (fe->st.st_mode == 0))
    {
        vfs_path_t *fname_vpath;

        fname_vpath = vfs_path_from_str (fe->fname);
        if (!do_cd (fname_vpath, cd_exact))
            message (D_ERROR, MSG_ERROR, _("Cannot change directory"));
        vfs_path_free (fname_vpath);
        return TRUE;
    }

    full_name_vpath = vfs_path_append_new (current_panel->cwd_vpath, fe->fname, (char *) NULL);

    /* Try associated command */
    ok = regex_command (full_name_vpath, "Open") != 0;
    vfs_path_free (full_name_vpath);
    if (ok)
        return TRUE;

    /* Check if the file is executable */
    full_name_vpath = vfs_path_append_new (current_panel->cwd_vpath, fe->fname, (char *) NULL);
    ok = (is_exe (fe->st.st_mode) && if_link_is_exe (full_name_vpath, fe));
    vfs_path_free (full_name_vpath);
    if (!ok)
        return FALSE;

    if (confirm_execute
        && query_dialog (_("The Midnight Commander"), _("Do you really want to execute?"), D_NORMAL,
                         2, _("&Yes"), _("&No")) != 0)
        return TRUE;

    if (!vfs_current_is_local ())
    {
        int ret;
        vfs_path_t *tmp_vpath;

        tmp_vpath = vfs_path_append_new (vfs_get_raw_current_dir (), fe->fname, (char *) NULL);
        ret = mc_setctl (tmp_vpath, VFS_SETCTL_RUN, NULL);
        vfs_path_free (tmp_vpath);
        /* We took action only if the dialog was shown or the execution was successful */
        return confirm_execute || (ret == 0);
    }

    {
        char *tmp, *cmd;

        tmp = name_quote (fe->fname, FALSE);
        cmd = g_strconcat (".", PATH_SEP_STR, tmp, (char *) NULL);
        g_free (tmp);
        shell_execute (cmd, 0);
        g_free (cmd);
    }

#ifdef HAVE_CHARSET
    mc_global.source_codepage = default_source_codepage;
#endif

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static inline gboolean
do_enter (WPanel * panel)
{
    return do_enter_on_file_entry (selection (panel));
}


/* --------------------------------------------------------------------------------------------- */

static void
panel_cycle_listing_format (WPanel * panel)
{
    panel->list_format = (panel->list_format + 1) % LIST_FORMATS;

    if (set_panel_formats (panel) == 0)
        do_refresh ();
}

/* --------------------------------------------------------------------------------------------- */

static void
chdir_other_panel (WPanel * panel)
{
    const file_entry_t *entry = &panel->dir.list[panel->selected];
    vfs_path_t *new_dir_vpath;
    char *sel_entry = NULL;

    if (get_other_type () != view_listing)
        create_panel (get_other_index (), view_listing);

    if (S_ISDIR (entry->st.st_mode) || link_isdir (entry))
        new_dir_vpath = vfs_path_append_new (panel->cwd_vpath, entry->fname, (char *) NULL);
    else
    {
        new_dir_vpath = vfs_path_append_new (panel->cwd_vpath, "..", (char *) NULL);
        sel_entry = strrchr (vfs_path_get_last_path_str (panel->cwd_vpath), PATH_SEP);
    }

    change_panel ();
    do_cd (new_dir_vpath, cd_exact);
    vfs_path_free (new_dir_vpath);

    if (sel_entry)
        try_to_select (current_panel, sel_entry);
    change_panel ();

    move_down (panel);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Make the current directory of the current panel also the current
 * directory of the other panel.  Put the other panel to the listing
 * mode if needed.  If the current panel is panelized, the other panel
 * doesn't become panelized.
 */

static void
panel_sync_other (const WPanel * panel)
{
    if (get_other_type () != view_listing)
        create_panel (get_other_index (), view_listing);

    do_panel_cd (other_panel, current_panel->cwd_vpath, cd_exact);

    /* try to select current filename on the other panel */
    if (!panel->is_panelized)
        try_to_select (other_panel, selection (panel)->fname);
}

/* --------------------------------------------------------------------------------------------- */

static void
chdir_to_readlink (WPanel * panel)
{
    vfs_path_t *new_dir_vpath;
    char buffer[MC_MAXPATHLEN];
    int i;
    struct stat st;
    vfs_path_t *panel_fname_vpath;
    gboolean ok;

    if (get_other_type () != view_listing)
        return;

    if (!S_ISLNK (panel->dir.list[panel->selected].st.st_mode))
        return;

    i = readlink (selection (panel)->fname, buffer, MC_MAXPATHLEN - 1);
    if (i < 0)
        return;

    panel_fname_vpath = vfs_path_from_str (selection (panel)->fname);
    ok = (mc_stat (panel_fname_vpath, &st) >= 0);
    vfs_path_free (panel_fname_vpath);
    if (!ok)
        return;

    buffer[i] = '\0';
    if (!S_ISDIR (st.st_mode))
    {
        char *p;

        p = strrchr (buffer, PATH_SEP);
        if (p != NULL && p[1] == '\0')
        {
            *p = '\0';
            p = strrchr (buffer, PATH_SEP);
        }
        if (p == NULL)
            return;

        p[1] = '\0';
    }
    if (IS_PATH_SEP (*buffer))
        new_dir_vpath = vfs_path_from_str (buffer);
    else
        new_dir_vpath = vfs_path_append_new (panel->cwd_vpath, buffer, (char *) NULL);

    change_panel ();
    do_cd (new_dir_vpath, cd_exact);
    vfs_path_free (new_dir_vpath);
    change_panel ();

    move_down (panel);
}

/* --------------------------------------------------------------------------------------------- */
/**
   function return 0 if not found and REAL_INDEX+1 if found
 */

static gsize
panel_get_format_field_index_by_name (const WPanel * panel, const char *name)
{
    GSList *format;
    gsize lc_index;

    for (lc_index = 1, format = panel->format;
         format != NULL && strcmp (((format_item_t *) format->data)->title, name) != 0;
         format = g_slist_next (format), lc_index++)
        ;

    if (format == NULL)
        lc_index = 0;

    return lc_index;
}

/* --------------------------------------------------------------------------------------------- */

static const panel_field_t *
panel_get_sortable_field_by_format (const WPanel * panel, gsize lc_index)
{
    const panel_field_t *pfield;
    const format_item_t *format;

    format = (const format_item_t *) g_slist_nth_data (panel->format, lc_index);
    if (format == NULL)
        return NULL;

    pfield = panel_get_field_by_title (format->title);
    if (pfield == NULL)
        return NULL;
    if (pfield->sort_routine == NULL)
        return NULL;
    return pfield;
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_toggle_sort_order_prev (WPanel * panel)
{
    gsize lc_index, i;
    const char *title;
    const panel_field_t *pfield = NULL;

    title = panel_get_title_without_hotkey (panel->sort_field->title_hotkey);
    lc_index = panel_get_format_field_index_by_name (panel, title);

    if (lc_index > 1)
    {
        /* search for prev sortable column in panel format */
        for (i = lc_index - 1;
             i != 0 && (pfield = panel_get_sortable_field_by_format (panel, i - 1)) == NULL; i--)
            ;
    }

    if (pfield == NULL)
    {
        /* Sortable field not found. Try to search in each array */
        for (i = g_slist_length (panel->format);
             i != 0 && (pfield = panel_get_sortable_field_by_format (panel, i - 1)) == NULL; i--)
            ;
    }

    if (pfield != NULL)
    {
        panel->sort_field = pfield;
        panel_set_sort_order (panel, pfield);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_toggle_sort_order_next (WPanel * panel)
{
    gsize lc_index, i;
    const panel_field_t *pfield = NULL;
    gsize format_field_count;
    const char *title;

    format_field_count = g_slist_length (panel->format);
    title = panel_get_title_without_hotkey (panel->sort_field->title_hotkey);
    lc_index = panel_get_format_field_index_by_name (panel, title);

    if (lc_index != 0 && lc_index != format_field_count)
    {
        /* search for prev sortable column in panel format */
        for (i = lc_index;
             i != format_field_count
             && (pfield = panel_get_sortable_field_by_format (panel, i)) == NULL; i++)
            ;
    }

    if (pfield == NULL)
    {
        /* Sortable field not found. Try to search in each array */
        for (i = 0;
             i != format_field_count
             && (pfield = panel_get_sortable_field_by_format (panel, i)) == NULL; i++)
            ;
    }

    if (pfield != NULL)
    {
        panel->sort_field = pfield;
        panel_set_sort_order (panel, pfield);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_select_sort_order (WPanel * panel)
{
    const panel_field_t *sort_order;

    sort_order = sort_box (&panel->sort_info, panel->sort_field);
    if (sort_order != NULL)
    {
        panel->sort_field = sort_order;
        panel_set_sort_order (panel, sort_order);
    }
}

/* --------------------------------------------------------------------------------------------- */

/**
 * panel_content_scroll_left:
 * @param panel the pointer to the panel on which we operate
 *
 * scroll long filename to the left (decrement scroll pointer)
 *
 */

static void
panel_content_scroll_left (WPanel * panel)
{
    if (panel->content_shift > -1)
    {
        if (panel->content_shift > panel->max_shift)
            panel->content_shift = panel->max_shift;

        panel->content_shift--;
        show_dir (panel);
        paint_dir (panel);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * panel_content_scroll_right:
 * @param panel the pointer to the panel on which we operate
 *
 * scroll long filename to the right (increment scroll pointer)
 *
 */

static void
panel_content_scroll_right (WPanel * panel)
{
    if (panel->content_shift < 0 || panel->content_shift < panel->max_shift)
    {
        panel->content_shift++;
        show_dir (panel);
        paint_dir (panel);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_set_sort_type_by_id (WPanel * panel, const char *name)
{
    if (strcmp (panel->sort_field->id, name) == 0)
        panel->sort_info.reverse = !panel->sort_info.reverse;
    else
    {
        const panel_field_t *sort_order;

        sort_order = panel_get_field_by_id (name);
        if (sort_order == NULL)
            return;

        panel->sort_field = sort_order;
    }

    panel_set_sort_order (panel, panel->sort_field);
}

/* --------------------------------------------------------------------------------------------- */
/**
 *  If we moved to the parent directory move the selection pointer to
 *  the old directory name; If we leave VFS dir, remove FS specificator.
 *
 *  You do _NOT_ want to add any vfs aware code here. <pavel@ucw.cz>
 */

static const char *
get_parent_dir_name (const vfs_path_t * cwd_vpath, const vfs_path_t * lwd_vpath)
{
    size_t llen, clen;
    const char *p, *lwd;

    llen = vfs_path_len (lwd_vpath);
    clen = vfs_path_len (cwd_vpath);

    if (llen <= clen)
        return NULL;

    lwd = vfs_path_as_str (lwd_vpath);

    p = g_strrstr (lwd, VFS_PATH_URL_DELIMITER);

    if (p == NULL)
    {
        const char *cwd;

        cwd = vfs_path_as_str (cwd_vpath);

        p = strrchr (lwd, PATH_SEP);

        if (p != NULL && strncmp (cwd, lwd, (size_t) (p - lwd)) == 0
            && (clen == (size_t) (p - lwd) || (p == lwd && IS_PATH_SEP (cwd[0]) && cwd[1] == '\0')))
            return (p + 1);

        return NULL;
    }

    /* skip VFS prefix */
    while (--p > lwd && !IS_PATH_SEP (*p))
        ;
    /* get last component */
    while (--p > lwd && !IS_PATH_SEP (*p))
        ;

    /* return last component */
    return (p != lwd || IS_PATH_SEP (*p)) ? p + 1 : p;
}

/* --------------------------------------------------------------------------------------------- */
/** Wrapper for do_subshell_chdir, check for availability of subshell */

static void
subshell_chdir (const vfs_path_t * vpath)
{
#ifdef ENABLE_SUBSHELL
    if (mc_global.tty.use_subshell && vfs_current_is_local ())
        do_subshell_chdir (vpath, FALSE);
#else /* ENABLE_SUBSHELL */
    (void) vpath;
#endif /* ENABLE_SUBSHELL */
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Changes the current directory of the panel.
 * Don't record change in the directory history.
 */

static gboolean
_do_panel_cd (WPanel * panel, const vfs_path_t * new_dir_vpath, enum cd_enum cd_type)
{
    vfs_path_t *olddir_vpath;

    /* Convert *new_path to a suitable pathname, handle ~user */
    if (cd_type == cd_parse_command)
    {
        const vfs_path_element_t *element;

        element = vfs_path_get_by_index (new_dir_vpath, 0);
        if (strcmp (element->path, "-") == 0)
            new_dir_vpath = panel->lwd_vpath;
    }

    if (mc_chdir (new_dir_vpath) == -1)
        return FALSE;

    /* Success: save previous directory, shutdown status of previous dir */
    olddir_vpath = vfs_path_clone (panel->cwd_vpath);
    panel_set_lwd (panel, panel->cwd_vpath);
    input_complete_free (cmdline);

    vfs_path_free (panel->cwd_vpath);
    vfs_setup_cwd ();
    panel->cwd_vpath = vfs_path_clone (vfs_get_raw_current_dir ());

    vfs_release_path (olddir_vpath);

    subshell_chdir (panel->cwd_vpath);

    /* Reload current panel */
    panel_clean_dir (panel);

    if (!dir_list_load (&panel->dir, panel->cwd_vpath, panel->sort_field->sort_routine,
                        &panel->sort_info, panel->filter))
        message (D_ERROR, MSG_ERROR, _("Cannot read directory contents"));

    try_to_select (panel, get_parent_dir_name (panel->cwd_vpath, olddir_vpath));

    load_hint (FALSE);
    panel->dirty = 1;
    update_xterm_title_path ();

    vfs_path_free (olddir_vpath);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
directory_history_next (WPanel * panel)
{
    gboolean ok;

    do
    {
        GList *next;

        ok = TRUE;
        next = g_list_next (panel->dir_history_current);
        if (next != NULL)
        {
            vfs_path_t *data_vpath;

            data_vpath = vfs_path_from_str ((char *) next->data);
            ok = _do_panel_cd (panel, data_vpath, cd_exact);
            vfs_path_free (data_vpath);
            panel->dir_history_current = next;
        }
        /* skip directories that present in history but absent in file system */
    }
    while (!ok);
}

/* --------------------------------------------------------------------------------------------- */

static void
directory_history_prev (WPanel * panel)
{
    gboolean ok;

    do
    {
        GList *prev;

        ok = TRUE;
        prev = g_list_previous (panel->dir_history_current);
        if (prev != NULL)
        {
            vfs_path_t *data_vpath;

            data_vpath = vfs_path_from_str ((char *) prev->data);
            ok = _do_panel_cd (panel, data_vpath, cd_exact);
            vfs_path_free (data_vpath);
            panel->dir_history_current = prev;
        }
        /* skip directories that present in history but absent in file system */
    }
    while (!ok);
}

/* --------------------------------------------------------------------------------------------- */

static void
directory_history_list (WPanel * panel)
{
    history_descriptor_t hd;
    gboolean ok = FALSE;
    size_t pos;

    pos = g_list_position (panel->dir_history_current, panel->dir_history);

    history_descriptor_init (&hd, WIDGET (panel)->y, WIDGET (panel)->x, panel->dir_history,
                             (int) pos);
    history_show (&hd);

    panel->dir_history = hd.list;
    if (hd.text != NULL)
    {
        vfs_path_t *s_vpath;

        s_vpath = vfs_path_from_str (hd.text);
        ok = _do_panel_cd (panel, s_vpath, cd_exact);
        if (ok)
            directory_history_add (panel, panel->cwd_vpath);
        else
            message (D_ERROR, MSG_ERROR, _("Cannot change directory"));
        vfs_path_free (s_vpath);
        g_free (hd.text);
    }

    if (!ok)
    {
        /* Since history is fully modified in history_show(), panel->dir_history actually
         * points to the invalid place. Try restore current postition here. */

        size_t i;

        panel->dir_history_current = panel->dir_history;

        for (i = 0; i <= pos; i++)
        {
            GList *prev;

            prev = g_list_previous (panel->dir_history_current);
            if (prev == NULL)
                break;

            panel->dir_history_current = prev;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
panel_execute_cmd (WPanel * panel, long command)
{
    int res = MSG_HANDLED;

    if (command != CK_Search)
        stop_search (panel);

    switch (command)
    {
    case CK_Up:
    case CK_Down:
    case CK_Left:
    case CK_Right:
    case CK_Bottom:
    case CK_Top:
    case CK_PageDown:
    case CK_PageUp:
        /* reset state of marks flag */
        state_mark = -1;
        break;
    default:
        break;
    }

    switch (command)
    {
    case CK_CycleListingFormat:
        panel_cycle_listing_format (panel);
        break;
    case CK_PanelOtherCd:
        chdir_other_panel (panel);
        break;
    case CK_PanelOtherCdLink:
        chdir_to_readlink (panel);
        break;
    case CK_CopySingle:
        copy_cmd_local ();
        break;
    case CK_DeleteSingle:
        delete_cmd_local ();
        break;
    case CK_Enter:
        do_enter (panel);
        break;
    case CK_ViewRaw:
        view_raw_cmd ();
        break;
    case CK_EditNew:
        edit_cmd_new ();
        break;
    case CK_MoveSingle:
        rename_cmd_local ();
        break;
    case CK_SelectInvert:
        panel_select_invert_files (panel);
        break;
    case CK_Select:
        panel_select_files (panel);
        break;
    case CK_SelectExt:
        panel_select_ext_cmd ();
        break;
    case CK_Unselect:
        panel_unselect_files (panel);
        break;
    case CK_PageDown:
        next_page (panel);
        break;
    case CK_PageUp:
        prev_page (panel);
        break;
    case CK_CdChild:
        goto_child_dir (panel);
        break;
    case CK_CdParent:
        goto_parent_dir (panel);
        break;
    case CK_History:
        directory_history_list (panel);
        break;
    case CK_HistoryNext:
        directory_history_next (panel);
        break;
    case CK_HistoryPrev:
        directory_history_prev (panel);
        break;
    case CK_BottomOnScreen:
        goto_bottom_file (panel);
        break;
    case CK_MiddleOnScreen:
        goto_middle_file (panel);
        break;
    case CK_TopOnScreen:
        goto_top_file (panel);
        break;
    case CK_Mark:
        mark_file (panel);
        break;
    case CK_MarkUp:
        mark_file_up (panel);
        break;
    case CK_MarkDown:
        mark_file_down (panel);
        break;
    case CK_MarkLeft:
        mark_file_left (panel);
        break;
    case CK_MarkRight:
        mark_file_right (panel);
        break;
    case CK_CdParentSmart:
        res = force_maybe_cd ();
        break;
    case CK_Up:
        move_up (panel);
        break;
    case CK_Down:
        move_down (panel);
        break;
    case CK_Left:
        res = move_left (panel);
        break;
    case CK_Right:
        res = move_right (panel);
        break;
    case CK_Bottom:
        move_end (panel);
        break;
    case CK_Top:
        move_home (panel);
        break;
#ifdef HAVE_CHARSET
    case CK_SelectCodepage:
        panel_change_encoding (panel);
        break;
#endif
    case CK_ScrollLeft:
        panel_content_scroll_left (panel);
        break;
    case CK_ScrollRight:
        panel_content_scroll_right (panel);
        break;
    case CK_Search:
        start_search (panel);
        break;
    case CK_SearchStop:
        break;
    case CK_PanelOtherSync:
        panel_sync_other (panel);
        break;
    case CK_Sort:
        panel_select_sort_order (panel);
        break;
    case CK_SortPrev:
        panel_toggle_sort_order_prev (panel);
        break;
    case CK_SortNext:
        panel_toggle_sort_order_next (panel);
        break;
    case CK_SortReverse:
        panel->sort_info.reverse = !panel->sort_info.reverse;
        panel_set_sort_order (panel, panel->sort_field);
        break;
    case CK_SortByName:
        panel_set_sort_type_by_id (panel, "name");
        break;
    case CK_SortByExt:
        panel_set_sort_type_by_id (panel, "extension");
        break;
    case CK_SortBySize:
        panel_set_sort_type_by_id (panel, "size");
        break;
    case CK_SortByMTime:
        panel_set_sort_type_by_id (panel, "mtime");
        break;
    default:
        res = MSG_NOT_HANDLED;
        break;
    }

    return res;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
panel_key (WPanel * panel, int key)
{
    long command;

    if (is_abort_char (key))
    {
        stop_search (panel);
        return MSG_HANDLED;
    }

    if (panel->searching && ((key >= ' ' && key <= 255) || key == KEY_BACKSPACE))
    {
        do_search (panel, key);
        return MSG_HANDLED;
    }

    command = widget_lookup_key (WIDGET (panel), key);
    if (command != CK_IgnoreKey)
        return panel_execute_cmd (panel, command);

    if (panels_options.torben_fj_mode && key == ALT ('h'))
    {
        goto_middle_file (panel);
        return MSG_HANDLED;
    }

    if (!command_prompt && ((key >= ' ' && key <= 255) || key == KEY_BACKSPACE))
    {
        start_search (panel);
        do_search (panel, key);
        return MSG_HANDLED;
    }

    return MSG_NOT_HANDLED;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
panel_callback (Widget * w, Widget * sender, widget_msg_t msg, int parm, void *data)
{
    WPanel *panel = PANEL (w);
    WDialog *h = DIALOG (w->owner);
    WButtonBar *bb;

    switch (msg)
    {
    case MSG_INIT:
        /* subscribe to "history_load" event */
        mc_event_add (h->event_group, MCEVENT_HISTORY_LOAD, panel_load_history, w, NULL);
        /* subscribe to "history_save" event */
        mc_event_add (h->event_group, MCEVENT_HISTORY_SAVE, panel_save_history, w, NULL);
        return MSG_HANDLED;

    case MSG_DRAW:
        /* Repaint everything, including frame and separator */
        widget_erase (w);
        show_dir (panel);
        panel_print_header (panel);
        adjust_top_file (panel);
#ifdef WITH_TABS
        draw_tabs (get_other_type () != view_listing
                   || get_current_type () != view_listing ? panel : NULL);
#endif
        paint_dir (panel);
        mini_info_separator (panel);
        display_mini_info (panel);
        panel->dirty = 0;
        return MSG_HANDLED;

    case MSG_FOCUS:
        state_mark = -1;
        current_panel = panel;
        panel->active = 1;

        if (mc_chdir (panel->cwd_vpath) != 0)
        {
            char *cwd;

            cwd = vfs_path_to_str_flags (panel->cwd_vpath, 0, VPF_STRIP_PASSWORD);
            message (D_ERROR, MSG_ERROR, _("Cannot chdir to \"%s\"\n%s"),
                     cwd, unix_error_string (errno));
            g_free (cwd);
        }
        else
            subshell_chdir (panel->cwd_vpath);

        update_xterm_title_path ();
        select_item (panel);

        bb = find_buttonbar (h);
        midnight_set_buttonbar (bb);
        widget_draw (WIDGET (bb));
#ifdef WITH_TABS
        draw_tabs (get_other_type () != view_listing
                   || get_current_type () != view_listing ? panel : NULL);
#endif
        return MSG_HANDLED;

    case MSG_UNFOCUS:
        /* Janne: look at this for the multiple panel options */
        stop_search (panel);
        panel->active = 0;
        unselect_item (panel);
        return MSG_HANDLED;

    case MSG_KEY:
        return panel_key (panel, parm);

    case MSG_ACTION:
        return panel_execute_cmd (panel, parm);

    case MSG_DESTROY:
        vfs_stamp_path (panel->cwd_vpath);
        /* unsubscribe from "history_load" event */
        mc_event_del (h->event_group, MCEVENT_HISTORY_LOAD, panel_load_history, w);
        /* unsubscribe from "history_save" event */
        mc_event_del (h->event_group, MCEVENT_HISTORY_SAVE, panel_save_history, w);
        panel_destroy (panel);
        free_my_statfs ();
        return MSG_HANDLED;

    default:
        return widget_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*                                     */
/* Panel mouse events support routines */
/*                                     */

static void
mouse_toggle_mark (WPanel * panel)
{
    do_mark_file (panel, MARK_DONT_MOVE);
    mouse_marking = selection (panel)->f.marked;
    mouse_mark_panel = current_panel;
}

/* --------------------------------------------------------------------------------------------- */

static void
mouse_set_mark (WPanel * panel)
{
    if (mouse_mark_panel == panel)
    {
        if (mouse_marking && !selection (panel)->f.marked)
            do_mark_file (panel, MARK_DONT_MOVE);
        else if (!mouse_marking && selection (panel)->f.marked)
            do_mark_file (panel, MARK_DONT_MOVE);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mark_if_marking (WPanel * panel, const mouse_event_t * event)
{
    if ((event->buttons & GPM_B_RIGHT) != 0)
    {
        if (event->msg == MSG_MOUSE_DOWN)
            mouse_toggle_mark (panel);
        else
            mouse_set_mark (panel);
        return TRUE;
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/** Determine which column was clicked, and sort the panel on
 * that column, or reverse sort on that column if already
 * sorted on that column.
 */

static void
mouse_sort_col (WPanel * panel, int x)
{
    int i = 0;
    GSList *format;
    const char *lc_sort_name = NULL;
    panel_field_t *col_sort_format = NULL;

    for (format = panel->format; format != NULL; format = g_slist_next (format))
    {
        format_item_t *fi = (format_item_t *) format->data;

        i += fi->field_len;
        if (x < i + 1)
        {
            /* found column */
            lc_sort_name = fi->title;
            break;
        }
    }

    if (lc_sort_name == NULL)
        return;

    for (i = 0; panel_fields[i].id != NULL; i++)
    {
        const char *title;

        title = panel_get_title_without_hotkey (panel_fields[i].title_hotkey);
        if (panel_fields[i].sort_routine != NULL && strcmp (title, lc_sort_name) == 0)
        {
            col_sort_format = &panel_fields[i];
            break;
        }
    }

    if (col_sort_format != NULL)
    {
        if (panel->sort_field == col_sort_format)
            /* reverse the sort if clicked column is already the sorted column */
            panel->sort_info.reverse = !panel->sort_info.reverse;
        else
            /* new sort is forced to be ascending */
            panel->sort_info.reverse = FALSE;

        panel_set_sort_order (panel, col_sort_format);
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
panel_mouse_is_on_item (const WPanel * panel, int y, int x)
{
    int last;

#ifdef WITH_TABS
    y -= TABS_UP(panel) ? TABS_UP_VLINES : 0;
#endif

    if (y < 0)
        return (-1);

    last = panel->dir.len - 1;
    y += panel->top_file;

    if (y > last)
        return (-1);

    if (panel->list_cols > 1)
    {
        int width, lines;

        width = (WIDGET (panel)->cols - 2) / panel->list_cols;
        lines = panel_lines (panel);
        y += lines * (x / width);
    }

    return (y > last ? -1 : y);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_mouse_callback (Widget * w, mouse_msg_t msg, mouse_event_t * event)
{
    WPanel *panel = PANEL (w);
    gboolean is_active;

    is_active = widget_is_active (w);

    switch (msg)
    {
    case MSG_MOUSE_DOWN:
        if (event->y == 0)
        {
            /* top frame */
            if (event->x == 1)
                /* "<" button */
                directory_history_prev (panel);
            else if (event->x == w->cols - 2)
                /* ">" button */
                directory_history_next (panel);
            else if (event->x >= w->cols - 5 && event->x <= w->cols - 3)
                /* "^" button */
                directory_history_list (panel);
            else if (event->x == w->cols - 6)
                /* "." button show/hide hidden files */
                send_message (midnight_dlg, NULL, MSG_ACTION, CK_ShowHidden, NULL);
            else
            {
                /* no other events on 1st line, return MOU_UNHANDLED */
                event->result.abort = TRUE;
                /* avoid extra panel redraw */
                panel->dirty = 0;
            }
            break;
        }
#ifdef WITH_TABS
        else if (event->y == (TABS_UP(panel) ? TABS_UP_TABSVLINE : panel_lines(panel)+2))
        {
            process_tab_click(event->x);
        }
        else if (event->y == (TABS_UP(panel) ? (TABS_UP_VLINES+1) : 1))
#else
        if (event->y == 1)
#endif
        {
            /* sort on clicked column */
            mouse_sort_col (panel, event->x + 1);
            break;
        }

        if (!is_active)
            change_panel ();
        MC_FALLTHROUGH;

    case MSG_MOUSE_DRAG:
        {
            int my_index;

            my_index = panel_mouse_is_on_item (panel, event->y - 2, event->x);
            if (my_index >= 0)
            {
                if (my_index != panel->selected)
                {
                    unselect_item (panel);
                    panel->selected = my_index;
                    select_item (panel);
                }

                /* This one is new */
                mark_if_marking (panel, event);
            }
        }
        break;

    case MSG_MOUSE_UP:
        break;

    case MSG_MOUSE_CLICK:
        if ((event->count & GPM_DOUBLE) != 0 && (event->buttons & GPM_B_LEFT) != 0 &&
            panel_mouse_is_on_item (panel, event->y - 2, event->x) >= 0)
            do_enter (panel);
        break;

    case MSG_MOUSE_MOVE:
        break;

    case MSG_MOUSE_SCROLL_UP:
        if (is_active)
        {
            if (panels_options.mouse_move_pages && panel->top_file > 0)
                prev_page (panel);
            else                /* We are in first page */
                move_up (panel);
        }
        break;

    case MSG_MOUSE_SCROLL_DOWN:
        if (is_active)
        {
            if (panels_options.mouse_move_pages
                && panel->top_file + panel_items (panel) < panel->dir.len)
                next_page (panel);
            else                /* We are in last page */
                move_down (panel);
        }
        break;

    default:
        break;
    }

    if (panel->dirty)
        widget_draw (w);
}

/* --------------------------------------------------------------------------------------------- */

static void
reload_panelized (WPanel * panel)
{
    int i, j;
    dir_list *list = &panel->dir;

    /* refresh current VFS directory required for vfs_path_from_str() */
    (void) mc_chdir (panel->cwd_vpath);

    for (i = 0, j = 0; i < list->len; i++)
    {
        vfs_path_t *vpath;

        vpath = vfs_path_from_str (list->list[i].fname);
        if (mc_lstat (vpath, &list->list[i].st) != 0)
            g_free (list->list[i].fname);
        else
        {
            if (j != i)
                list->list[j] = list->list[i];
            j++;
        }
        vfs_path_free (vpath);
    }
    if (j == 0)
        dir_list_init (list);
    else
        list->len = j;

    recalculate_panel_summary (panel);

    if (panel != current_panel)
        (void) mc_chdir (current_panel->cwd_vpath);
}

/* --------------------------------------------------------------------------------------------- */

static void
update_one_panel_widget (WPanel * panel, panel_update_flags_t flags, const char *current_file)
{
    gboolean free_pointer;
    char *my_current_file = NULL;

    if ((flags & UP_RELOAD) != 0)
    {
        panel->is_panelized = FALSE;
        mc_setctl (panel->cwd_vpath, VFS_SETCTL_FLUSH, NULL);
        memset (&(panel->dir_stat), 0, sizeof (panel->dir_stat));
    }

    /* If current_file == -1 (an invalid pointer) then preserve selection */
    free_pointer = current_file == UP_KEEPSEL;

    if (free_pointer)
    {
        my_current_file = g_strdup (panel->dir.list[panel->selected].fname);
        current_file = my_current_file;
    }

    if (panel->is_panelized)
        reload_panelized (panel);
    else
        panel_reload (panel);

    try_to_select (panel, current_file);
    panel->dirty = 1;

    if (free_pointer)
        g_free (my_current_file);
}

/* --------------------------------------------------------------------------------------------- */

static void
update_one_panel (int which, panel_update_flags_t flags, const char *current_file)
{
    if (get_panel_type (which) == view_listing)
    {
        WPanel *panel;

        panel = PANEL (get_panel_widget (which));
        if (panel->is_panelized)
            flags &= ~UP_RELOAD;
        update_one_panel_widget (panel, flags, current_file);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
do_select (WPanel * panel, int i)
{
    if (i != panel->selected)
    {
        panel->dirty = 1;
        panel->selected = i;
        panel->top_file = panel->selected - (WIDGET (panel)->lines - 2) / 2;
        if (panel->top_file < 0)
            panel->top_file = 0;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
do_try_to_select (WPanel * panel, const char *name)
{
    int i;
    char *subdir;

    if (name == NULL)
    {
        do_select (panel, 0);
        return;
    }

    /* We only want the last component of the directory,
     * and from this only the name without suffix.
     * Cut prefix if the panel is not panelized */

    if (panel->is_panelized)
        subdir = vfs_strip_suffix_from_filename (name);
    else
        subdir = vfs_strip_suffix_from_filename (x_basename (name));

    /* Search that subdir or filename without prefix (if not panelized panel), select it if found */
    for (i = 0; i < panel->dir.len; i++)
    {
        if (strcmp (subdir, panel->dir.list[i].fname) == 0)
        {
            do_select (panel, i);
            g_free (subdir);
            return;
        }
    }

    /* Try to select a file near the file that is missing */
    if (panel->selected >= panel->dir.len)
        do_select (panel, panel->dir.len - 1);
    g_free (subdir);
}

/* --------------------------------------------------------------------------------------------- */

/* event callback */
static gboolean
event_update_panels (const gchar * event_group_name, const gchar * event_name,
                     gpointer init_data, gpointer data)
{
    (void) event_group_name;
    (void) event_name;
    (void) init_data;
    (void) data;

    update_panels (UP_RELOAD, UP_KEEPSEL);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* event callback */
static gboolean
panel_save_current_file_to_clip_file (const gchar * event_group_name, const gchar * event_name,
                                      gpointer init_data, gpointer data)
{
    (void) event_group_name;
    (void) event_name;
    (void) init_data;
    (void) data;

    if (current_panel->marked == 0)
        mc_event_raise (MCEVENT_GROUP_CORE, "clipboard_text_to_file",
                        (gpointer) selection (current_panel)->fname);
    else
    {
        int i;
        gboolean first = TRUE;
        char *flist = NULL;

        for (i = 0; i < current_panel->dir.len; i++)
            if (current_panel->dir.list[i].f.marked != 0)
            {                   /* Skip the unmarked ones */
                if (first)
                {
                    flist = g_strdup (current_panel->dir.list[i].fname);
                    first = FALSE;
                }
                else
                {
                    /* Add empty lines after the file */
                    char *tmp;

                    tmp =
                        g_strconcat (flist, "\n", current_panel->dir.list[i].fname, (char *) NULL);
                    g_free (flist);
                    flist = tmp;
                }
            }

        mc_event_raise (MCEVENT_GROUP_CORE, "clipboard_text_to_file", (gpointer) flist);
        g_free (flist);
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static vfs_path_t *
panel_recursive_cd_to_parent (const vfs_path_t * vpath)
{
    vfs_path_t *cwd_vpath;

    cwd_vpath = vfs_path_clone (vpath);

    while (mc_chdir (cwd_vpath) < 0)
    {
        const char *panel_cwd_path;
        vfs_path_t *tmp_vpath;

        /* check if path contains only '/' */
        panel_cwd_path = vfs_path_as_str (cwd_vpath);
        if (panel_cwd_path != NULL && IS_PATH_SEP (panel_cwd_path[0]) && panel_cwd_path[1] == '\0')
            return NULL;

        tmp_vpath = vfs_path_vtokens_get (cwd_vpath, 0, -1);
        vfs_path_free (cwd_vpath);
        cwd_vpath =
            vfs_path_build_filename (PATH_SEP_STR, vfs_path_as_str (tmp_vpath), (char *) NULL);
        vfs_path_free (tmp_vpath);
    }

    return cwd_vpath;
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_dir_list_callback (dir_list_cb_state_t state, void *data)
{
    static int count = 0;

    (void) data;

    switch (state)
    {
    case DIR_OPEN:
        count = 0;
        break;

    case DIR_READ:
        count++;
        if ((count & 15) == 0)
            rotate_dash (TRUE);
        break;

    case DIR_CLOSE:
        rotate_dash (FALSE);
        break;

    default:
        g_assert_not_reached ();
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
try_to_select (WPanel * panel, const char *name)
{
    do_try_to_select (panel, name);
    select_item (panel);
}

/* --------------------------------------------------------------------------------------------- */

void
panel_clean_dir (WPanel * panel)
{
    panel->top_file = 0;
    panel->selected = 0;
    panel->marked = 0;
    panel->dirs_marked = 0;
    panel->total = 0;
    panel->searching = FALSE;
    panel->is_panelized = FALSE;
    panel->dirty = 1;
    panel->content_shift = -1;
    panel->max_shift = -1;

    dir_list_free_list (&panel->dir);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Set Up panel's current dir object
 *
 * @param panel panel object
 * @param path_str string contain path
 */

void
panel_set_cwd (WPanel * panel, const vfs_path_t * vpath)
{
    if (vpath != panel->cwd_vpath)      /* check if new vpath is not the panel->cwd_vpath object */
    {
        vfs_path_free (panel->cwd_vpath);
        panel->cwd_vpath = vfs_path_clone (vpath);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Set Up panel's last working dir object
 *
 * @param panel panel object
 * @param path_str string contain path
 */

void
panel_set_lwd (WPanel * panel, const vfs_path_t * vpath)
{
    if (vpath != panel->lwd_vpath)      /* check if new vpath is not the panel->lwd_vpath object */
    {
        vfs_path_free (panel->lwd_vpath);
        panel->lwd_vpath = vfs_path_clone (vpath);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Creatie an empty panel with specified size.
 *
 * @param panel_name name of panel for setup retieving
 *
 * @return new instance of WPanel
 */

WPanel *
panel_sized_empty_new (const char *panel_name, int y, int x, int lines, int cols)
{
    WPanel *panel;
    Widget *w;
    char *section;
    int i, err;

    panel = g_new0 (WPanel, 1);
    w = WIDGET (panel);
    widget_init (w, y, x, lines, cols, panel_callback, panel_mouse_callback);
    w->options |= WOP_SELECTABLE | WOP_TOP_SELECT;
    w->keymap = panel_map;

    panel->dir.size = DIR_LIST_MIN_SIZE;
    panel->dir.list = g_new (file_entry_t, panel->dir.size);
    panel->dir.len = 0;
    panel->dir.callback = panel_dir_list_callback;

    panel->list_cols = 1;
    panel->brief_cols = 2;
    panel->dirty = 1;
    panel->content_shift = -1;
    panel->max_shift = -1;

    panel->list_format = list_full;
    panel->user_format = g_strdup (DEFAULT_USER_FORMAT);

    for (i = 0; i < LIST_FORMATS; i++)
        panel->user_status_format[i] = g_strdup (DEFAULT_USER_FORMAT);

#ifdef HAVE_CHARSET
    panel->codepage = SELECT_CHARSET_NO_TRANSLATE;
#endif

    panel->frame_size = frame_half;

    panel->panel_name = g_strdup (panel_name);
    panel->hist_name = g_strconcat ("Dir Hist ", panel->panel_name, (char *) NULL);
    /* directories history will be get later */

    section = g_strconcat ("Temporal:", panel->panel_name, (char *) NULL);
    if (!mc_config_has_group (mc_global.main_config, section))
    {
        g_free (section);
        section = g_strdup (panel->panel_name);
    }
    panel_load_setup (panel, section);
#ifdef WITH_TABS
    if (saved_tabs != NULL)
    {
        panel->tabs.list = saved_tabs;
        panel->tabs.current = saved_tabs;
        saved_tabs = NULL;
    }
    else
    {
        create_tab (panel, TABDIR_NEXT, NULL);
    }
    panel->tabs.do_not_delete = 0;
#endif
    g_free (section);

    /* Load format strings */
    err = set_panel_formats (panel);
    if (err != 0)
        set_panel_formats (panel);

    return panel;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Panel creation for specified size and directory.
 *
 * @param panel_name name of panel for setup retieving
 * @param y y coordinate of top-left corner
 * @param x x coordinate of top-left corner
 * @param lines vertical size
 * @param cols horizontal size
 * @param vpath working panel directory. If NULL then current directory is used
 *
 * @return new instance of WPanel
 */

WPanel *
panel_sized_with_dir_new (const char *panel_name, int y, int x, int lines, int cols,
                          const vfs_path_t * vpath)
{
    WPanel *panel;
    char *curdir = NULL;
#ifdef HAVE_CHARSET
    const vfs_path_element_t *path_element;
#endif

    panel = panel_sized_empty_new (panel_name, y, x, lines, cols);

    if (vpath != NULL)
    {
        curdir = _vfs_get_cwd ();
        panel_set_cwd (panel, vpath);
    }
    else
    {
        vfs_setup_cwd ();
        panel->cwd_vpath = vfs_path_clone (vfs_get_raw_current_dir ());
    }

    panel_set_lwd (panel, vfs_get_raw_current_dir ());

#ifdef HAVE_CHARSET
    path_element = vfs_path_get_by_index (panel->cwd_vpath, -1);
    if (path_element->encoding != NULL)
        panel->codepage = get_codepage_index (path_element->encoding);
#endif

    if (mc_chdir (panel->cwd_vpath) != 0)
    {
#ifdef HAVE_CHARSET
        panel->codepage = SELECT_CHARSET_NO_TRANSLATE;
#endif
        vfs_setup_cwd ();
        vfs_path_free (panel->cwd_vpath);
        panel->cwd_vpath = vfs_path_clone (vfs_get_raw_current_dir ());
    }

    /* Load the default format */
    if (!dir_list_load (&panel->dir, panel->cwd_vpath, panel->sort_field->sort_routine,
                        &panel->sort_info, panel->filter))
        message (D_ERROR, MSG_ERROR, _("Cannot read directory contents"));

    /* Restore old right path */
    if (curdir != NULL)
    {
        vfs_path_t *tmp_vpath;
        int err;

        tmp_vpath = vfs_path_from_str (curdir);
        mc_chdir (tmp_vpath);
        vfs_path_free (tmp_vpath);
        (void) err;
    }
    g_free (curdir);

    return panel;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_reload (WPanel * panel)
{
    struct stat current_stat;
    vfs_path_t *cwd_vpath;

    if (panels_options.fast_reload && stat (vfs_path_as_str (panel->cwd_vpath), &current_stat) == 0
        && current_stat.st_ctime == panel->dir_stat.st_ctime
        && current_stat.st_mtime == panel->dir_stat.st_mtime)
        return;

    cwd_vpath = panel_recursive_cd_to_parent (panel->cwd_vpath);
    vfs_path_free (panel->cwd_vpath);

    if (cwd_vpath == NULL)
    {
        panel->cwd_vpath = vfs_path_from_str (PATH_SEP_STR);
        panel_clean_dir (panel);
        dir_list_init (&panel->dir);
        return;
    }

    panel->cwd_vpath = cwd_vpath;
    memset (&(panel->dir_stat), 0, sizeof (panel->dir_stat));
    show_dir (panel);

    if (!dir_list_reload (&panel->dir, panel->cwd_vpath, panel->sort_field->sort_routine,
                          &panel->sort_info, panel->filter))
        message (D_ERROR, MSG_ERROR, _("Cannot read directory contents"));

    panel->dirty = 1;
    if (panel->selected >= panel->dir.len)
        do_select (panel, panel->dir.len - 1);

    recalculate_panel_summary (panel);
}

/* --------------------------------------------------------------------------------------------- */
/* Switches the panel to the mode specified in the format           */
/* Seting up both format and status string. Return: 0 - on success; */
/* 1 - format error; 2 - status error; 3 - errors in both formats.  */

int
set_panel_formats (WPanel * p)
{
    GSList *form;
    char *err = NULL;
    int retcode = 0;

    form = use_display_format (p, panel_format (p), &err, FALSE);

    if (err != NULL)
    {
        g_free (err);
        retcode = 1;
    }
    else
    {
        g_slist_free_full (p->format, (GDestroyNotify) format_item_free);
        p->format = form;
    }

    if (panels_options.show_mini_info)
    {
        form = use_display_format (p, mini_status_format (p), &err, TRUE);

        if (err != NULL)
        {
            g_free (err);
            retcode += 2;
        }
        else
        {
            g_slist_free_full (p->status_format, (GDestroyNotify) format_item_free);
            p->status_format = form;
        }
    }

    panel_update_cols (WIDGET (p), p->frame_size);

    if (retcode)
        message (D_ERROR, _("Warning"),
                 _("User supplied format looks invalid, reverting to default."));
    if (retcode & 0x01)
    {
        g_free (p->user_format);
        p->user_format = g_strdup (DEFAULT_USER_FORMAT);
    }
    if (retcode & 0x02)
    {
        g_free (p->user_status_format[p->list_format]);
        p->user_status_format[p->list_format] = g_strdup (DEFAULT_USER_FORMAT);
    }

    return retcode;
}

/* --------------------------------------------------------------------------------------------- */

/* Select current item and readjust the panel */
void
select_item (WPanel * panel)
{
    adjust_top_file (panel);

    panel->dirty = 1;

    execute_hooks (select_file_hook);
}

/* --------------------------------------------------------------------------------------------- */
/** Clears all files in the panel, used only when one file was marked */
void
unmark_files (WPanel * panel)
{
    if (panel->marked != 0)
    {
        int i;

        for (i = 0; i < panel->dir.len; i++)
            file_mark (panel, i, 0);

        panel->dirs_marked = 0;
        panel->marked = 0;
        panel->total = 0;
    }
}

/* --------------------------------------------------------------------------------------------- */
/** Recalculate the panels summary information, used e.g. when marked
   files might have been removed by an external command */

void
recalculate_panel_summary (WPanel * panel)
{
    int i;

    panel->marked = 0;
    panel->dirs_marked = 0;
    panel->total = 0;

    for (i = 0; i < panel->dir.len; i++)
        if (panel->dir.list[i].f.marked)
        {
            /* do_file_mark will return immediately if newmark == oldmark.
               So we have to first unmark it to get panel's summary information
               updated. (Norbert) */
            panel->dir.list[i].f.marked = 0;
            do_file_mark (panel, i, 1);
        }
}

/* --------------------------------------------------------------------------------------------- */
/** This routine marks a file or a directory */

void
do_file_mark (WPanel * panel, int idx, int mark)
{
    if (panel->dir.list[idx].f.marked == mark)
        return;

    /* Only '..' can't be marked, '.' isn't visible */
    if (DIR_IS_DOTDOT (panel->dir.list[idx].fname))
        return;

    file_mark (panel, idx, mark);
    if (panel->dir.list[idx].f.marked)
    {
        panel->marked++;

        if (S_ISDIR (panel->dir.list[idx].st.st_mode))
        {
            if (panel->dir.list[idx].f.dir_size_computed)
                panel->total += (uintmax_t) panel->dir.list[idx].st.st_size;
            panel->dirs_marked++;
        }
        else
            panel->total += (uintmax_t) panel->dir.list[idx].st.st_size;

        set_colors (panel);
    }
    else
    {
        if (S_ISDIR (panel->dir.list[idx].st.st_mode))
        {
            if (panel->dir.list[idx].f.dir_size_computed)
                panel->total -= (uintmax_t) panel->dir.list[idx].st.st_size;
            panel->dirs_marked--;
        }
        else
            panel->total -= (uintmax_t) panel->dir.list[idx].st.st_size;

        panel->marked--;
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Changes the current directory of the panel.
 * Record change in the directory history.
 */
gboolean
do_panel_cd (WPanel * panel, const vfs_path_t * new_dir_vpath, enum cd_enum cd_type)
{
    gboolean r;

    r = _do_panel_cd (panel, new_dir_vpath, cd_type);
    if (r)
        directory_history_add (panel, panel->cwd_vpath);
    return r;
}

/* --------------------------------------------------------------------------------------------- */

void
file_mark (WPanel * panel, int lc_index, int val)
{
    if (panel->dir.list[lc_index].f.marked != val)
    {
        panel->dir.list[lc_index].f.marked = val;
        panel->dirty = 1;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
panel_re_sort (WPanel * panel)
{
    char *filename;
    int i;

    if (panel == NULL)
        return;

    filename = g_strdup (selection (panel)->fname);
    unselect_item (panel);
    dir_list_sort (&panel->dir, panel->sort_field->sort_routine, &panel->sort_info);
    panel->selected = -1;

    for (i = panel->dir.len; i != 0; i--)
        if (strcmp (panel->dir.list[i - 1].fname, filename) == 0)
        {
            panel->selected = i - 1;
            break;
        }

    g_free (filename);
    panel->top_file = panel->selected - panel_items (panel) / 2;
    select_item (panel);
    panel->dirty = 1;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_set_sort_order (WPanel * panel, const panel_field_t * sort_order)
{
    if (sort_order == NULL)
        return;

    panel->sort_field = sort_order;

    /* The directory is already sorted, we have to load the unsorted stuff */
    if (sort_order->sort_routine == (GCompareFunc) unsorted)
    {
        char *current_file;

        current_file = g_strdup (panel->dir.list[panel->selected].fname);
        panel_reload (panel);
        try_to_select (panel, current_file);
        g_free (current_file);
    }
    panel_re_sort (panel);
}

/* --------------------------------------------------------------------------------------------- */

#ifdef HAVE_CHARSET

/**
 * Change panel encoding.
 * @param panel WPanel object
 */

void
panel_change_encoding (WPanel * panel)
{
    const char *encoding = NULL;
    char *errmsg;
    int r;

    r = select_charset (-1, -1, panel->codepage, FALSE);

    if (r == SELECT_CHARSET_CANCEL)
        return;                 /* Cancel */

    panel->codepage = r;

    if (panel->codepage == SELECT_CHARSET_NO_TRANSLATE)
    {
        /* No translation */
        vfs_path_t *cd_path_vpath;

        g_free (init_translation_table (mc_global.display_codepage, mc_global.display_codepage));
        cd_path_vpath = remove_encoding_from_path (panel->cwd_vpath);
        do_panel_cd (panel, cd_path_vpath, cd_parse_command);
        show_dir (panel);
        vfs_path_free (cd_path_vpath);
        return;
    }

    errmsg = init_translation_table (panel->codepage, mc_global.display_codepage);
    if (errmsg != NULL)
    {
        message (D_ERROR, MSG_ERROR, "%s", errmsg);
        g_free (errmsg);
        return;
    }

    encoding = get_codepage_id (panel->codepage);
    if (encoding != NULL)
    {
        vfs_path_change_encoding (panel->cwd_vpath, encoding);

        if (!do_panel_cd (panel, panel->cwd_vpath, cd_parse_command))
            message (D_ERROR, MSG_ERROR, _("Cannot chdir to \"%s\""),
                     vfs_path_as_str (panel->cwd_vpath));
    }
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Remove encode info from last path element.
 *
 */
vfs_path_t *
remove_encoding_from_path (const vfs_path_t * vpath)
{
    vfs_path_t *ret_vpath;
    GString *tmp_conv;
    int indx;

    ret_vpath = vfs_path_new ();

    tmp_conv = g_string_new ("");

    for (indx = 0; indx < vfs_path_elements_count (vpath); indx++)
    {
        GIConv converter;
        vfs_path_element_t *path_element;

        path_element = vfs_path_element_clone (vfs_path_get_by_index (vpath, indx));

        if (path_element->encoding == NULL)
        {
            vfs_path_add_element (ret_vpath, path_element);
            continue;
        }

        converter = str_crt_conv_to (path_element->encoding);
        if (converter == INVALID_CONV)
        {
            vfs_path_add_element (ret_vpath, path_element);
            continue;
        }

        MC_PTR_FREE (path_element->encoding);

        str_vfs_convert_from (converter, path_element->path, tmp_conv);

        g_free (path_element->path);
        path_element->path = g_strndup (tmp_conv->str, tmp_conv->len);

        g_string_set_size (tmp_conv, 0);

        str_close_conv (converter);
        str_close_conv (path_element->dir.converter);
        path_element->dir.converter = INVALID_CONV;
        vfs_path_add_element (ret_vpath, path_element);
    }
    g_string_free (tmp_conv, TRUE);
    return ret_vpath;
}
#endif /* HAVE_CHARSET */

/* --------------------------------------------------------------------------------------------- */

/**
 * This routine reloads the directory in both panels. It tries to
 * select current_file in current_panel and other_file in other_panel.
 * If current_file == -1 then it automatically sets current_file and
 * other_file to the currently selected files in the panels.
 *
 * If flags has the UP_ONLY_CURRENT bit toggled on, then it
 * will not reload the other panel.
 *
 * @param flags for reload panel
 * @param current_file name of the current file
 */

void
update_panels (panel_update_flags_t flags, const char *current_file)
{
    WPanel *panel;

    /* first, update other panel... */
    if ((flags & UP_ONLY_CURRENT) == 0)
        update_one_panel (get_other_index (), flags, UP_KEEPSEL);
    /* ...then current one */
    update_one_panel (get_current_index (), flags, current_file);

    if (get_current_type () == view_listing)
        panel = PANEL (get_panel_widget (get_current_index ()));
    else
        panel = PANEL (get_panel_widget (get_other_index ()));

    if (!panel->is_panelized)
        (void) mc_chdir (panel->cwd_vpath);
}

/* --------------------------------------------------------------------------------------------- */

gsize
panel_get_num_of_sortable_fields (void)
{
    gsize ret = 0, lc_index;

    for (lc_index = 0; panel_fields[lc_index].id != NULL; lc_index++)
        if (panel_fields[lc_index].is_user_choice)
            ret++;
    return ret;
}

/* --------------------------------------------------------------------------------------------- */

char **
panel_get_sortable_fields (gsize * array_size)
{
    char **ret;
    gsize lc_index, i;

    lc_index = panel_get_num_of_sortable_fields ();

    ret = g_try_new0 (char *, lc_index + 1);
    if (ret == NULL)
        return NULL;

    if (array_size != NULL)
        *array_size = lc_index;

    lc_index = 0;

    for (i = 0; panel_fields[i].id != NULL; i++)
        if (panel_fields[i].is_user_choice)
            ret[lc_index++] = g_strdup (_(panel_fields[i].title_hotkey));

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

const panel_field_t *
panel_get_field_by_id (const char *name)
{
    gsize lc_index;

    for (lc_index = 0; panel_fields[lc_index].id != NULL; lc_index++)
        if (panel_fields[lc_index].id != NULL && strcmp (name, panel_fields[lc_index].id) == 0)
            return &panel_fields[lc_index];

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const panel_field_t *
panel_get_field_by_title_hotkey (const char *name)
{
    gsize lc_index;

    for (lc_index = 0; panel_fields[lc_index].id != NULL; lc_index++)
        if (panel_fields[lc_index].title_hotkey != NULL &&
            strcmp (name, _(panel_fields[lc_index].title_hotkey)) == 0)
            return &panel_fields[lc_index];

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const panel_field_t *
panel_get_field_by_title (const char *name)
{
    gsize lc_index;

    for (lc_index = 0; panel_fields[lc_index].id != NULL; lc_index++)
    {
        const char *title;

        title = panel_get_title_without_hotkey (panel_fields[lc_index].title_hotkey);
        if (strcmp (title, name) == 0)
            return &panel_fields[lc_index];
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

gsize
panel_get_num_of_user_possible_fields (void)
{
    gsize ret = 0, lc_index;

    for (lc_index = 0; panel_fields[lc_index].id != NULL; lc_index++)
        if (panel_fields[lc_index].use_in_user_format)
            ret++;

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

char **
panel_get_user_possible_fields (gsize * array_size)
{
    char **ret;
    gsize lc_index, i;

    lc_index = panel_get_num_of_user_possible_fields ();

    ret = g_try_new0 (char *, lc_index + 1);
    if (ret == NULL)
        return NULL;

    if (array_size != NULL)
        *array_size = lc_index;

    lc_index = 0;

    for (i = 0; panel_fields[i].id != NULL; i++)
        if (panel_fields[i].use_in_user_format)
            ret[lc_index++] = g_strdup (_(panel_fields[i].title_hotkey));

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_init (void)
{
    panel_sort_up_char = mc_skin_get ("widget-panel", "sort-up-char", "'");
    panel_sort_down_char = mc_skin_get ("widget-panel", "sort-down-char", ".");
    panel_hiddenfiles_show_char = mc_skin_get ("widget-panel", "hiddenfiles-show-char", ".");
    panel_hiddenfiles_hide_char = mc_skin_get ("widget-panel", "hiddenfiles-hide-char", ".");
    panel_history_prev_item_char = mc_skin_get ("widget-panel", "history-prev-item-char", "<");
    panel_history_next_item_char = mc_skin_get ("widget-panel", "history-next-item-char", ">");
    panel_history_show_list_char = mc_skin_get ("widget-panel", "history-show-list-char", "^");
    panel_filename_scroll_left_char =
        mc_skin_get ("widget-panel", "filename-scroll-left-char", "{");
    panel_filename_scroll_right_char =
        mc_skin_get ("widget-panel", "filename-scroll-right-char", "}");

    mc_event_add (MCEVENT_GROUP_FILEMANAGER, "update_panels", event_update_panels, NULL, NULL);
    mc_event_add (MCEVENT_GROUP_FILEMANAGER, "panel_save_current_file_to_clip_file",
                  panel_save_current_file_to_clip_file, NULL, NULL);
}

/* --------------------------------------------------------------------------------------------- */

void
panel_deinit (void)
{
    g_free (panel_sort_up_char);
    g_free (panel_sort_down_char);
    g_free (panel_hiddenfiles_show_char);
    g_free (panel_hiddenfiles_hide_char);
    g_free (panel_history_prev_item_char);
    g_free (panel_history_next_item_char);
    g_free (panel_history_show_list_char);
    g_free (panel_filename_scroll_left_char);
    g_free (panel_filename_scroll_right_char);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
do_cd (const vfs_path_t * new_dir_vpath, enum cd_enum exact)
{
    gboolean res;
    const vfs_path_t *_new_dir_vpath = new_dir_vpath;

    if (current_panel->is_panelized)
    {
        size_t new_vpath_len;

        new_vpath_len = vfs_path_len (new_dir_vpath);
        if (vfs_path_equal_len (new_dir_vpath, panelized_panel.root_vpath, new_vpath_len))
            _new_dir_vpath = panelized_panel.root_vpath;
    }

    res = do_panel_cd (current_panel, _new_dir_vpath, exact);

#ifdef HAVE_CHARSET
    if (res)
    {
        const vfs_path_element_t *path_element;

        path_element = vfs_path_get_by_index (current_panel->cwd_vpath, -1);
        if (path_element->encoding != NULL)
            current_panel->codepage = get_codepage_index (path_element->encoding);
        else
            current_panel->codepage = SELECT_CHARSET_NO_TRANSLATE;
    }
#endif /* HAVE_CHARSET */

    return res;
}

#ifdef WITH_TABS
/**
 * Gets the new tabs direction from preferences
 */
TabsDirection_t get_new_tabs_direction ()
{

    if (tabs_options.open_where == BEFORE_CURRENT)
    {
        return TABDIR_PREV;
    }
    else if (tabs_options.open_where == AFTER_CURRENT)
    {
        return TABDIR_NEXT;
    }
    else if (tabs_options.open_where == AT_END)
    {
        return TABDIR_LAST;
    }
    else if (tabs_options.open_where == AT_BEGINNING)
    {
        return TABDIR_FIRST;
    }

    return TABDIR_LAST;
}

/**
 * This function will create a new tab in the current panel
 */
void
new_tab (WPanel * panel)
{
    TabsDirection_t d = get_new_tabs_direction ();
    create_tab (panel, d, NULL);
    change_tab (panel, d, NULL);
    draw_tabs (panel);
    panel->dirty = 1;
}

/**
 * Copies the current tab from the current panel to the other Panel
 */
void
copy_tab_to_other_panel (void)
{
    WPanel *opanel = (WPanel *) other_panel;
    WPanel *cpanel = (WPanel *) current_panel;
    Tab *tc, *to;
    TabsDirection_t d = get_new_tabs_direction ();

    tc = cpanel->tabs.current->data;
    create_tab (opanel, d, NULL);
    change_tab (opanel, d, NULL);
    to = opanel->tabs.current->data;

    if (tc->name != NULL)
    {
        to->name = g_new0 (char, strlen (tc->name) + 1);
        strcpy (to->name, tc->name);
    }
    to->path = vfs_path_clone (cpanel->cwd_vpath);
    do_panel_cd (opanel, to->path, cd_exact);
    draw_tabs (opanel);
}

void
swap_tabs (void)
{
    WPanel *panel1 = (WPanel *) other_panel;
    WPanel *panel2 = (WPanel *) current_panel;
    GList *b, *e, *d, *f;
    // We consider the tabs on the current panel a, b, c
    // and the tabs on the other panel d, e, f
    // We try to change b and e.
    // If we have only one tab on each panel, then we do switch panel.
    // We have a special case if we have only one tab on only one of the panels.
    // Then this algorithm will work if panel1 is the panel with only one tab
    // If we changed the first tab of any of the panel, we have to also change the panel->tabs.list pointer

    if (panel1->tabs.current->next == panel1->tabs.current
        && panel2->tabs.current->next == panel2->tabs.current)
    {
        swap_panels ();
        tty_touch_screen ();
        repaint_screen ();
        return;
    }
    else
    {
        if (panel2->tabs.current->next == panel2->tabs.current)
        {
            panel1 = (WPanel *) current_panel;
            panel2 = (WPanel *) other_panel;
        }
    }

    b = panel1->tabs.current;
    ((Tab *) b->data)->path = vfs_path_clone (panel1->cwd_vpath);
    e = panel2->tabs.current;
    ((Tab *) e->data)->path = vfs_path_clone (panel2->cwd_vpath);
    d = e->prev;
    f = e->next;

    if (b->prev != b)
    {
        b->prev->next = e;
    }
    e->next = b->next == b ? e : b->next;
    if (b->next != b)
    {
        b->next->prev = e;
    }
    e->prev = b->prev == b ? e : b->prev;

    d->next = b;
    b->next = f;
    f->prev = b;
    b->prev = d;

    panel1->tabs.current = e;
    if (panel1->tabs.list == b)
    {
        panel1->tabs.list = e;
    }
    panel2->tabs.current = b;
    if (panel2->tabs.list == e)
    {
        panel2->tabs.list = b;
    }
    do_panel_cd (panel1, ((Tab *) e->data)->path, cd_exact);
    do_panel_cd (panel2, ((Tab *) b->data)->path, cd_exact);
    change_panel ();
    repaint_screen ();
}

/**
 * Moves the current tab from the current panel to the other Panel
 */
void
move_tab_to_other_panel (void)
{
    WPanel *opanel = (WPanel *) other_panel;
    WPanel *cpanel = (WPanel *) current_panel;
    Tab *t;
    GList *i;
    TabsDirection_t d = get_new_tabs_direction ();
    int first_tab = 0;

    if (cpanel->tabs.list->next == cpanel->tabs.list)
    {
        message (D_ERROR, MSG_ERROR, _("The current tab is the only one.\nYou cannot move it."));
    }
    else
    {
        if (cpanel->tabs.list == cpanel->tabs.current)
        {
            cpanel->tabs.list = cpanel->tabs.list->next;
            first_tab = 1;
        }
        t = cpanel->tabs.current->data;
        create_tab (opanel, d, (Tab *) t);
        i = cpanel->tabs.current;
        change_tab (cpanel, TABDIR_PREV, NULL);
        i->prev->next = i->next;
        i->next->prev = i->prev;
        g_free (i);
        i = opanel->tabs.current->next;
        while (i != opanel->tabs.current)
        {
            if (i->data == t)
            {
                break;
            }

            i = i->next;
        }
        change_tab (opanel, TABDIR_ABSOLUTE, i);
        change_panel ();
        if (first_tab)
        {
            change_tab (cpanel, TABDIR_ABSOLUTE, cpanel->tabs.list);
        }
        repaint_screen ();
    }
}
#endif
/**
 * This function will close a tab
 * @return void
 */
void
close_tab (WPanel * p)
{
    if (p->tabs.list->next == p->tabs.list)
    {
        message (D_ERROR, MSG_ERROR, _("The current tab is the only one.\nYou cannot close it."));
    }
    else
    {
        int first = p->tabs.current == p->tabs.list;
        GList *b = p->tabs.current->prev;
        GList *c = p->tabs.current;
        GList *d = p->tabs.current->next;
        change_tab (p, TABDIR_PREV, NULL);
        b->next = d;
        d->prev = b;

        destroy_tab (c->data);
        g_free (c);

        if (first)
        {
            p->tabs.list = d;
        }
    }
}

/**
 * This function renames a tab
 * @return void
 */
void
rename_tab (WPanel * p)
{
    Tab *t = (Tab *) p->tabs.current->data;
    char *title = get_tab_title(p, t, 30);

    char *name = input_dialog (_("Tab rename"), _("Please enter the new name:"), NULL, title,
                               INPUT_COMPLETE_NONE);
    log4c(log4ccat, LOG4C_PRIORITY_INFO, "rename");
    if (name)
    {
        if (strlen (name))
        {
            t->name = g_new0 (char, strlen (name) + 1);
            strcpy (t->name, name);
            g_free (name);
        }
    }
    widget_draw ((Widget *) p);
}

int
get_tab_index (WPanel * p, GList * t)
{
    int result = 0;
    GList *i = p->tabs.list;
    do
    {
        if (i == t)
        {
            return result;
        }
        result++;
        i = i->next;
    }
    while (i != p->tabs.list);

    return 0;
}

/**
 * This function goes to an arbitrary choosen tab
 * @return void
 */
void
goto_tab (WPanel * p)
{
    Listbox *listbox =
        create_listbox_window (15, 60, _("Available Tabs"), "[Available Tabs Selector]");
    GList *it = p->tabs.list;
    int items = 0;
    int result;

    log4c(log4ccat, LOG4C_PRIORITY_INFO, "goto");
    do
    {
        char *title = TAB_TITLE (p, it->data);
        listbox_add_item (listbox->list, LISTBOX_APPEND_AT_END, 0, title, (void *) it, FALSE);
        it = it->next;
        items++;
    }
    while (it != p->tabs.list);

    listbox_select_entry (listbox->list, get_tab_index (current_panel, p->tabs.current));

    result = run_listbox (listbox);

    if (result >= 0)
    {
        GList *t;
        t = g_list_nth ((GList *) p->tabs.list, result);
        change_tab (p, TABDIR_ABSOLUTE, t);
    }
}

void
create_tab (WPanel *p, TabsDirection_t direction, Tab *tab)
{
    GList *pTabListItem = NULL;
    GList *newTabListItem = g_new0 (GList, 1);

    log4c(log4ccat, LOG4C_PRIORITY_INFO, "create");
    if (tab == NULL)
    {
        tab = g_new0 (Tab, 1);
        tab->path = NULL;
        tab->name = NULL;
    }
    newTabListItem->data = tab;

    /* get the pointer of the target position */
    if (direction == TABDIR_NEXT)
    {
        pTabListItem = p->tabs.current;
    }
    else if (direction == TABDIR_PREV)
    {
        pTabListItem = p->tabs.current->prev;
    }
    else if (direction == TABDIR_LAST || direction == TABDIR_FIRST)
    {
        pTabListItem = p->tabs.list ? p->tabs.list->prev : NULL;
    }

    /* tab list already exists */
    if (pTabListItem)
    {
        GList *d = pTabListItem->next;
        pTabListItem->next = newTabListItem;
        newTabListItem->next = d;
        d->prev = newTabListItem;
        newTabListItem->prev = pTabListItem;
    }
    else
    {
        newTabListItem->next = newTabListItem;
        newTabListItem->prev = newTabListItem;
        p->tabs.current = newTabListItem;
    }


    if (!p->tabs.list || (direction == TABDIR_FIRST))
    {
        p->tabs.list = newTabListItem;
    }
}


GList *
get_tab_by_index (WPanel * p, int idx)
{
    int i = 0;
    GList *t = p->tabs.list;

    while (i++ < idx)
    {
        t = t->next;
    }

    return t;
}

RestoredTabs
_restore_tabs (FILE * f)
{
    RestoredTabs result;
    Tab *t;
    int idx = 0;
    int crt_idx;
	int fscanf_items_read;
    GList *i;
    GList *j = NULL;
    char *line = g_new0 (char, 1024);

    result.error = 0;
    result.list = NULL;
    fscanf_items_read = fscanf (f, "%d\n%d\n", &result.idx, &crt_idx);

    if ((crt_idx == -1) || (fscanf_items_read != 2))
    {
        return result;
    }

    do
    {
        read_line (line, 1024, f);
        if (strcmp (line, "") && !feof (f))
        {
            t = g_new0 (Tab, 1);
            if (!strcmp (line, "(null)"))
            {
                t->name = NULL;
            }
            else
            {
                t->name = g_new0 (char, strlen (line) + 1);
                memcpy (t->name, line, strlen (line) + 1);
            }

            read_line (line, 1024, f);
            if (!strcmp (line, ""))
            {
                result.error = 1;
                return result;
            }
            t->path = vfs_path_from_str (line);

            i = g_new0 (GList, 1);
            i->data = (void *) t;

            if (result.list == NULL)
            {
                result.list = i;
                result.list->prev = result.list;
                j = i;
            }
            i->next = result.list;
            result.list->prev = i;
            i->prev = j;
            j->next = i;
            j = i;

            if (idx++ == crt_idx)
            {
                result.current = j;
            }
        }
    }
    while (!feof (f) && strcmp (line, "") && !result.error);

    g_free (line);

    return result;
}

void
abort_restore (RestoredTabs restored)
{
    WPanel *tmp = g_new0 (WPanel, 1);
    tmp->tabs.list = restored.list;
    destroy_tabs (tmp);
    message (D_ERROR, MSG_ERROR, _("Error restoring the tabs."));
    g_free (tmp);
}

void
read_line (char *line, int max, FILE * f)
{
	if (NULL != fgets (line, max, f))
	{
	    if (line[strlen (line) - 1] == '\n')
	    {
	        line[strlen (line) - 1] = '\0';
	    }
	}
}
void
restore_tabs_session (char *title)
{
    char *file_name = g_new0 (char, strlen (tabs_options.sessions_folder) + 1 + strlen (title) + 1);
    RestoredTabs restored1, restored2;
    char *line = g_new0 (char, 1024);
    Tab *t;

    restored1.list = restored1.current = restored2.list = restored2.current = NULL;

    sprintf (file_name, "%s/%s", tabs_options.sessions_folder, title);

    if (exist_file (file_name))
    {
        FILE *f = fopen (file_name, "rt");
        read_line (line, 1024, f);
        if (strcmp (line, "[Current Panel]"))
        {
            abort_restore (restored1);
            g_free (line);
            g_free (file_name);
            return;
        }

        restored1 = _restore_tabs (f);
        if (restored1.error)
        {
            abort_restore (restored1);
            g_free (line);
            g_free (file_name);
            return;
        }
        read_line (line, 1024, f);
        if (strcmp (line, "[Other Panel]"))
        {
            abort_restore (restored1);
            g_free (line);
            g_free (file_name);
            return;
        }
        restored2 = _restore_tabs (f);
        if (restored2.error)
        {
            abort_restore (restored1);
            abort_restore (restored2);
            g_free (line);
            g_free (file_name);
            return;
        }

        fclose (f);
        log4c(log4ccat, LOG4C_PRIORITY_INFO, "Index: %d ; %d ;",get_current_index (), get_other_index ());
        log4c(log4ccat, LOG4C_PRIORITY_INFO, "Paneltype:  %du ; %du ;",get_panel_type (get_current_index ()), get_panel_type (get_other_index ()));
        if (restored1.list && (get_panel_type (get_current_index ()) == view_listing))
        {
            destroy_tabs (current_panel);
            current_panel->tabs.list = restored1.list;
            current_panel->tabs.current = restored1.current;

            t = (Tab *) current_panel->tabs.current->data;
            do_panel_cd (current_panel, t->path, cd_exact);

        }
        if (restored2.list && (get_panel_type (get_other_index ()) == view_listing))
        {
            destroy_tabs (other_panel);
            other_panel->tabs.list = restored2.list;
            other_panel->tabs.current = restored2.current;
            t = (Tab *) other_panel->tabs.current->data;
            do_panel_cd (other_panel, t->path, cd_exact);
        }

        if (get_current_index () != restored1.idx)
        {
            //swap_panels ();
            if (restored1.idx != 0)
            {
                //change_panel();
            }
        }
    }
    g_free (line);
    g_free (file_name);
}

void
_write_tabs (FILE * f, WPanel * p)
{
    GList *i = p->tabs.list;
    Tab *t;
    int crt = get_tab_index (p, p->tabs.current);

    fprintf (f, "%d\n", crt);
    do
    {
        t = (Tab *) i->data;
        fprintf (f, "%s\n%s\n", t->name ? t->name : "(null)",
                 vfs_path_as_str (i == p->tabs.current ? p->cwd_vpath : t->path));
        i = i->next;
    }
    while (i != p->tabs.list);
}

void
save_tabs_session (char *title)
{
    char *file_name = g_new0 (char, strlen (tabs_options.sessions_folder) + 1 + strlen (title) + 1);
    FILE *f;
    panel_view_mode_t t;
    int i;
    sprintf (file_name, "%s/%s", tabs_options.sessions_folder, title);

    f = fopen (file_name, "wt");

    i = get_current_index ();
    t = get_panel_type (i);
    fprintf (f, "[Current Panel]\n%d\n", i);
    if (t == view_listing)
    {
        _write_tabs (f, current_panel);
    }
    else
    {
        fprintf (f, "-1\n");
    }
    i = get_other_index ();
    t = get_panel_type (i);
    fprintf (f, "\n[Other Panel]\n%d\n", i);
    if (t == view_listing)
    {
        _write_tabs (f, other_panel);
    }
    else
    {
        fprintf (f, "-1\n");
    }
    fclose (f);
    g_free (file_name);
}
void
save_tabs_session_custom (void)
{
    char *name =
        input_dialog (_("Save custom tabs session"), _("Please enter the session name:"), NULL, "",
                      INPUT_COMPLETE_NONE);
    if (name)
    {
        if (strlen (name))
        {
            save_tabs_session (name);
        }

        g_free (name);
    }
}

void
restore_tabs_session_custom (void)
{
    Listbox *listbox =
        create_listbox_window (15, 60, _("Available Sessions"), "[Available Sessions Selector]");
    struct dirent *entry;
    DIR *dir;
    char *name;
    GList *items, *last, *current;
    int result;
    GList *t;
    GList *i;

    dir = opendir (tabs_options.sessions_folder);

    items = NULL;

    while ((entry = readdir (dir)) != NULL)
    {
        if (strcmp (entry->d_name, ".") && strcmp (entry->d_name, ".."))
        {
            name = g_new0 (char, 255);
            memmove (name, entry->d_name, strlen (entry->d_name) + 1);
            last = g_new0 (GList, 1);
            last->data = name;
            last->next = NULL;
            if (items == NULL)
            {
                items = last;
                current = last;
            }
            else
            {
                current->next = last;
            }
            current = last;
            listbox_add_item (listbox->list, LISTBOX_APPEND_AT_END, 0, entry->d_name,
                              (void *) name, FALSE);
        }
    }

    result = run_listbox (listbox);

    if (result >= 0)
    {
        t = g_list_nth ((GList *) items, result);
        restore_tabs_session ((char *) t->data);
    }

    i = items;
    while (i)
    {
        name = (char *) i->data;
        i = i->next;
        g_free (name);
    }
}

void
change_tab (WPanel * p, TabsDirection_t d, GList * tab)
{
    log4c(log4ccat, LOG4C_PRIORITY_INFO, "change");
    if (p->tabs.current)
    {
        Tab *t = (Tab *) p->tabs.current->data;
        if (t->path)
        {
            vfs_path_free (t->path);
        }

        //t->path = (vfs_path_t *) g_memdup(p->cwd_vpath, sizeof(vfs_path_t));
        t->path = vfs_path_clone (p->cwd_vpath);

        if (d == TABDIR_NEXT)
        {
            p->tabs.current = p->tabs.current->next;
        }
        else if (d == TABDIR_PREV)
        {
            p->tabs.current = p->tabs.current->prev;
        }
        else if (d == TABDIR_FIRST)
        {
            p->tabs.current = p->tabs.list;
        }
        else if (d == TABDIR_LAST)
        {
            p->tabs.current = p->tabs.list->prev;
        }
        else if (tab != NULL)
        {
            p->tabs.current = tab;
        }

        t = (Tab *) p->tabs.current->data;
        if (t->path)
        {
            do_panel_cd (p, t->path, cd_exact);
        }
    }
}

void
destroy_tab (Tab * t)
{
    log4c(log4ccat, LOG4C_PRIORITY_INFO, "destroy");
    if (t->path)
    {
        vfs_path_free (t->path);
    }
    if (t->name)
    {
        g_free (t->name);
    }
    g_free (t);
}

void
destroy_tabs (WPanel * p)
{
    GList *i;
    Tab *t;
    int loop;
    if (p->tabs.list == NULL || p->tabs.do_not_delete)
    {
        saved_tabs = p->tabs.list;
        p->tabs.do_not_delete = 0;
        return;
    }
    loop = p->tabs.list != p->tabs.list->next;
    p->tabs.list->prev->next = NULL;
    do
    {
        i = p->tabs.list;
        if (i)
        {
            t = (Tab *) i->data;
            p->tabs.list = p->tabs.list->next;
            destroy_tab (t);
            g_free (i);
        }
    }
    while (p->tabs.list && loop);
    p->tabs.list = NULL;
    p->tabs.current = NULL;
}

int
draw_tab (char *title, int selected)
{
    if (selected)
    {
        tty_setcolor (SELECTED_COLOR);
    }
    tty_print_string (" ");
    tty_print_string (title);
    tty_print_string (" ");
    tty_setcolor (NORMAL_COLOR);
    tty_print_one_vline (TRUE);

    return strlen (title) + 3;
}

void
cut_title (char *title, int max)
{
    title[max] = '\0';
    title[max - 1] = '.';
    title[max - 2] = '.';
    title[max - 3] = '.';
}

void TabMarkActive(WPanel * p, Tab * t, char *result);

void TabMarkActive(WPanel * p, Tab * t, char *result)
{
    if (HIGHLIGHT_TAB)
    {
        strcat (result, "*");
    }
    else
    {
        strcat (result, " ");
    }
}

char *
get_tab_title (WPanel * p, Tab * t, unsigned int max)
{
    char *result = g_new0 (char, 255);
    vfs_path_t *path;
    size_t c;
    Widget *w = (Widget *) p;

    if ( (max > (unsigned int) w->cols - 5) || (max == 0))
    {
        max = w->cols - 5;
    }
    //for highlight * or space
    max -= 1;

    /* check if name was already set */
    if (t->name)
    {

        log4c(log4ccat, LOG4C_PRIORITY_INFO, "Tabname: %s",t->name);
        strncpy (result, t->name, max);
        if (max != 0 && strlen (t->name) > max)
        {
            cut_title (result, max);
        }
        TabMarkActive(p, t, result);
        return result;
    }

    if (t != p->tabs.current->data)
    {
        if (!t->path)
        {
            strcpy (result, "Error");
            return result;
        }
        path = t->path;
    }
    else
    {
        path = p->cwd_vpath;
    }
    log4c(log4ccat, LOG4C_PRIORITY_INFO, "TabPath: %s",path->str);
    c = vfs_path_tokens_count (path);
    if (c == 0)
    {
        strcpy (result, "/");
        TabMarkActive(p, t, result);
        log4c(log4ccat, LOG4C_PRIORITY_INFO, "TabPath_Empty: %s",result);
        return result;
    }

    g_free (result);


    result = vfs_path_tokens_get (path, c - 1, 1);
    if (max != 0 && strlen (result) > max)
    {
        cut_title (result, max);
    }
    TabMarkActive(p, t, result);
    return result;
}

TabDisplayInfo *
display_info (WPanel * p, Widget * w)
{
    int max_length = w->cols - 2;
    TabDisplayInfo *result = g_new0 (TabDisplayInfo, 1);
    GList *i;
    int length = 0, dir = -1;
    char *title;
    char *buffer, *current = NULL, *j, *k;

    result->start_tab = result->end_tab = p->tabs.current;
    result->start_idx = 0;
    result->end_idx = -1;
    result->scroll = NO_SCROLL;

    if (p->tabs.list->next == p->tabs.list)
    {
        return result;
    }

    log4c(log4ccat, LOG4C_PRIORITY_INFO, "");
    i = p->tabs.list;
    do
    {
        title = TAB_TITLE (p, i->data);
        length += strlen (title) + 3;
        g_free (title);
        i = i->next;
    }
    while (i != p->tabs.list);

    i = p->tabs.list;
    buffer = g_new0 (char, length);

    i = p->tabs.list;
    j = buffer;
    do
    {
        title = TAB_TITLE (p, i->data);
        if (p->tabs.current == i)
        {
            current = j;
        }
        k = g_new0 (char, 2 + strlen (title));
        strcpy (k, " ");
        strcat (k, title);
        strcpy (title, k);
        strcat (title, " ");
        memcpy (j, title, strlen (title));
        g_free (k);
        j += strlen (title);
        j[0] = 0;
        j++;
        g_free (title);
        i = i->next;
    }
    while (i != p->tabs.list);

    length = strlen (current) + 3;
    j = current;
    k = current + strlen (j);

    while (length < max_length)
    {
        if (dir == -1)
        {
            if (j != buffer)
            {
                length++;
                j--;
                if (*j == 0)
                {
                    result->start_tab = result->start_tab->prev;
                }
            }
        }
        else if (dir == 1)
        {
            if (*k != 0 || result->end_tab->next != p->tabs.list)
            {
                if (*k == 0)
                {
                    result->end_tab = result->end_tab->next;
                }
                k++;
                length++;
            }
        }

        dir *= -1;

        if (j == buffer && *k == 0 && result->end_tab->next == p->tabs.list)
        {
            break;
        }
    }

    if (*j == 0)
    {
        result->start_idx = 0;
        if (result->start_tab != result->end_tab)
        {
            result->start_tab = result->start_tab->next;
        }
    }
    else
    {
        title = TAB_TITLE (p, result->start_tab->data);
        if (strcmp (j, " ") == 0)
        {
            result->start_idx = strlen (title) - 1;
        }
        else
        {
            result->start_idx = strlen (title) - strlen (j) + 1;
            if (strlen (title) == strlen (j) - 1 && result->start_tab != p->tabs.list)
            {
                result->scroll = result->scroll | SCROLL_LEFT;
            }
        }
        g_free (title);
    }

    if (*k == 0)
    {
        result->end_idx = -1;
    }
    else
    {
        title = TAB_TITLE (p, result->end_tab->data);
        if (strcmp (k, " ") == 0)
        {
            result->end_idx = strlen (title);
            result->scroll = result->scroll | SCROLL_RIGHT;
        }
        else
        {
            result->end_idx = strlen (title) - strlen (k) + 1;
            if (result->end_idx < 0)
            {
                result->end_idx = 0;
            }
        }
        g_free (title);
    }

    if (result->start_tab != p->tabs.list || result->start_idx > 0)
    {
        result->scroll = result->scroll | SCROLL_LEFT;
    }

    if (result->end_tab->next != p->tabs.list || result->end_idx != -1)
    {
        result->scroll = result->scroll | SCROLL_RIGHT;
    }

    g_free (buffer);

    return result;
}

void
draw_tabs (WPanel * panel)
{
    WPanel *p = (panel == NULL ? current_panel : panel);
    Widget *w;
    char *title;
    int x, y, length;
    GList *i;
    TabDisplayInfo *info;

    do
    {
        // if (current_panel->frame_size == frame_full && p != current_panel){
        //     break;
        // }
        if (TABS_VISIBLE (p))
        {
            if (p->tabs.list == NULL)
            {
                break;
            }
            tty_setcolor (NORMAL_COLOR);
            w = (Widget *) p;

            x = w->x + 1;
            log4c(log4ccat, LOG4C_PRIORITY_INFO, "");
            if (!TABS_UP (p))
            {
                y = w->y + w->lines - 3;        //w->lines - (the_menubar->is_visible ? 2 : 3);
                tty_draw_hline (y, x, ACS_HLINE, w->cols - 2);
                tty_gotoyx (y, w->x);
                tty_print_alt_char (ACS_LTEE, FALSE);
                tty_gotoyx (y, w->x + w->cols - 1);
                tty_print_alt_char (ACS_RTEE, FALSE);
                tty_gotoyx (y + 1, x);
            }
            else
            {
                y = w->y;
                tty_draw_hline (y + 1, x, ACS_HLINE, w->cols - 2);
                tty_draw_hline (y + 3, x, ACS_HLINE, w->cols - 2);
                tty_gotoyx (y + 1, w->x);
                tty_print_alt_char (ACS_LTEE, FALSE);
                tty_gotoyx (y + 1, w->x + w->cols - 1);
                tty_print_alt_char (ACS_RTEE, FALSE);
                tty_gotoyx (y + 3, w->x);
                tty_print_alt_char (ACS_LTEE, FALSE);
                tty_gotoyx (y + 3, w->x + w->cols - 1);
                tty_print_alt_char (ACS_RTEE, FALSE);
                tty_gotoyx (y + 2, x);
            }
            length = w->cols - 2;
            info = display_info (p, w);
            i = info->start_tab;
            if (i)
            {
                do
                {
                    title = TAB_TITLE (p, i->data);
                    if (i == info->start_tab && info->start_idx != 0)
                    {
                        int l = strlen (title) - info->start_idx;
                        char *title2 = g_new0 (char, l + 1);
                        memcpy (title2, title + info->start_idx, l + 1);
                        memcpy (title, title2, strlen (title2));
                        title[l] = '\0';
                        g_free (title2);
                    }
                    if (i == info->end_tab && info->end_idx != -1)
                    {
                        title[info->end_idx] = '\0';
                    }
                    if (*title != 0)
                    {
                        length -= draw_tab (title, i == p->tabs.current && p == current_panel);
                    }
                    g_free (title);
                    i = i->next;
                }
                while (i != info->end_tab->next);
            }
            g_free (info);

            tty_print_string (str_fit_to_term (" ", length, J_LEFT));

            // Draw the scroll signs
            if ((info->scroll & SCROLL_LEFT) == SCROLL_LEFT)
            {
                tty_gotoyx ((TABS_UP (p) ? w->y + 2 : w->lines - 1), w->x + 1);
                tty_print_string (panel_history_prev_item_char);
            }

            if ((info->scroll & SCROLL_RIGHT) == SCROLL_RIGHT)
            {
                tty_gotoyx ((TABS_UP (p) ? w->y + 2 : w->lines - 1), w->x + w->cols - 2);
                tty_print_string (panel_history_next_item_char);
            }
        }

        if (panel != NULL)
        {
            break;
        }

        if (p == other_panel)
        {
            p = NULL;
        }
        else
        {
            p = other_panel;
        }
    }
    while (p);
}

void
hide_tabs (void)
{
    WPanel *p = current_panel;
    Widget *w = WIDGET (p);

    int x = w->x + 1, y = w->lines - (the_menubar->is_visible ? 2 : 3);

    tty_gotoyx (y, x);
    tty_print_string (str_fit_to_term (" ", w->cols - 2, J_LEFT));
    tty_gotoyx (y + 1, x);
    tty_print_string (str_fit_to_term (" ", w->cols - 2, J_LEFT));
}

/*
 * Processes a click on the tabbar
 */
void
process_tab_click (unsigned int x)
{
    Widget *w = (Widget *) current_panel;
    TabDisplayInfo *info = display_info (current_panel, w);
    GList *i;
    char *title, *j;
    unsigned int n;

    log4c(log4ccat, LOG4C_PRIORITY_INFO, "event_x = %u", x);

    if (x == 2 && ((info->scroll & SCROLL_LEFT) == SCROLL_LEFT))
    {
        change_tab (current_panel, TABDIR_PREV, NULL);
    }
    else if (x == (unsigned int) (w->cols - 1) && ((info->scroll & SCROLL_RIGHT) == SCROLL_RIGHT))
    {
        change_tab (current_panel, TABDIR_NEXT, NULL);
    }
    else
    {
        n = 2;
        for (i = info->start_tab; i != info->end_tab; i = i->next)
        {
            title = TAB_TITLE (current_panel, i->data);
            if (!title)
            {
                strcpy(title, ((Tab *)i->data)->path->str);
            }
            j = title;
            log4c(log4ccat, LOG4C_PRIORITY_INFO, "Local title: %s ; Title: %s ; Path: %s", title, ((Tab *)i->data)->name, ((Tab *)i->data)->path->str);
            if (i == info->start_tab && info->start_idx != 0)
            {
                j = title + info->start_idx;
            }
            log4c(log4ccat, LOG4C_PRIORITY_INFO, "title = %s ; start_idx = %d", j, info->start_idx);
            if (x < strlen (j) + 2 + n)
            {
                g_free (title);
                break;
            }

            n += strlen (j) + 3;
            g_free (title);
        }

        change_tab (current_panel, TABDIR_ABSOLUTE, i);
    }
    g_free (info);
}

#if 0
void
move_tab (WPanel * p, TabsDirection_t d)
{
    gpointer i;
    if (p->tabs.list == p->tabs.list->next)
    {
        return;
    }

    if (d == TABDIR_PREV && p->tabs.current == p->tabs.list)
    {
        p->tabs.list = p->tabs.list->next;
        draw_tabs (p);
        return;
    }

    if (d == TABDIR_NEXT && p->tabs.current->next == p->tabs.list)
    {
        p->tabs.list = p->tabs.current;
        draw_tabs (p);
        return;
    }

    change_tab (p, TABDIR_ABSOLUTE, p->tabs.current);
    i = p->tabs.current->data;
    if (d == TABDIR_NEXT)
    {
        p->tabs.current->data = p->tabs.current->next->data;
        p->tabs.current->next->data = i;
    }
    else
    {
        p->tabs.current->data = p->tabs.current->prev->data;
        p->tabs.current->prev->data = i;
    }

    do_panel_cd (p, ((Tab *) p->tabs.current->data)->path, cd_exact);
    change_tab (p, d, NULL);
    draw_tabs (p);
}
#endif
/* --------------------------------------------------------------------------------------------- */
