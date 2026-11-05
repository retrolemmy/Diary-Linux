/* Diary for Linux (GTK 3): a simple one-page-per-day journal. */

#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "dates.h"
#include "server.h"
#include "store.h"
#include "undo.h"

#define kVersion   "1.0"
#define kWebsite   "https://github.com/retrolemmy/Diary-Linux"
#define kListWidth 220
#define kMargin    12

typedef struct {
    GtkWidget *window, *calendar, *list, *dateLabel, *editor;
    GtkWidget *saveItem, *showAllItem, *undoItem, *redoItem;
    GtkListStore *rows;
    GtkTextBuffer *buffer;
    GtkCssProvider *fontStyle;
    PangoFontDescription *font;     /* NULL: the theme's */
    Undo *undo;

    char *folder;
    char *address, *key;            /* the settings; address "" for the folder */
    Server *server;                 /* NULL: the folder (also if unreachable) */

    GArray *allDays, *shownDays;
    int day;                        /* the open entry */
    char *entryVersion;             /* its version on the server */
    char *search;                   /* NULL unless the list shows a search */

    gboolean syncing;               /* while Diary itself sets list and calendar */
    gboolean skipDaySelected;       /* the calendar changed months */
    guint calendarIdle;
    guint openTimer;                /* opens pendingDay after a click */
    int pendingDay;
    int width, height;
} Diary;

static Diary g;

/* ---------- Settings ---------- */

static char *SettingsPath(void)
{
    return g_build_filename(g_get_user_config_dir(), "diary", "diary.conf", NULL);
}

static void LoadSettings(void)
{
    GKeyFile *file = g_key_file_new();
    char *path = SettingsPath(), *font;

    g.width = 820;
    g.height = 560;
    if (g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL)) {
        font = g_key_file_get_string(file, "Diary", "Font", NULL);
        if (font != NULL && *font != 0)
            g.font = pango_font_description_from_string(font);
        g_free(font);
        g.address = g_key_file_get_string(file, "Diary", "Server", NULL);
        g.key = g_key_file_get_string(file, "Diary", "ServerKey", NULL);
        if (g_key_file_has_key(file, "Diary", "Width", NULL)) {
            g.width = MAX(g_key_file_get_integer(file, "Diary", "Width", NULL), 560);
            g.height = MAX(g_key_file_get_integer(file, "Diary", "Height", NULL), 400);
        }
    }
    if (g.address == NULL)
        g.address = g_strdup("");
    if (g.key == NULL)
        g.key = g_strdup("");
    g_key_file_free(file);
    g_free(path);
}

static void SaveSettings(void)
{
    GKeyFile *file = g_key_file_new();
    char *path = SettingsPath(), *folder = g_path_get_dirname(path), *font;

    if (g.font != NULL) {
        font = pango_font_description_to_string(g.font);
        g_key_file_set_string(file, "Diary", "Font", font);
        g_free(font);
    }
    g_key_file_set_string(file, "Diary", "Server", g.address);
    g_key_file_set_string(file, "Diary", "ServerKey", g.key);
    g_key_file_set_integer(file, "Diary", "Width", g.width);
    g_key_file_set_integer(file, "Diary", "Height", g.height);
    g_mkdir_with_parents(folder, 0700);
    if (g_key_file_save_to_file(file, path, NULL))
        g_chmod(path, 0600);        /* it holds the server's key */
    g_key_file_free(file);
    g_free(folder);
    g_free(path);
}

/* ---------- Messages ---------- */

static void ShowMessage(const char *title, const char *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g.window), GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s", title);

    if (message != NULL && *message != 0)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* TRUE if the user picks the second (default) button. */
static gboolean Ask(const char *title, const char *message, const char *no, const char *yes)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g.window), GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", title);
    int answer;

    if (message != NULL && *message != 0)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), no, GTK_RESPONSE_CANCEL, yes, GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    answer = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return answer == GTK_RESPONSE_OK;
}

/* Asks for one or more lines of text: labels has one label per field
   (NULL-terminated), values their texts, which get the answers. */
static gboolean AskText(const char *title, const char *const *labels, char **values)
{
    GtkWidget *dialog, *box, *fields[4];
    gboolean ok;
    int i, n;

    dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(g.window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), kMargin);
    gtk_box_set_spacing(GTK_BOX(box), 6);

    for (n = 0; labels[n] != NULL && n < 4; n++) {
        GtkWidget *label = gtk_label_new(labels[n]);

        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 50);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        if (n > 0)
            gtk_widget_set_margin_top(label, 6);
        gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
        fields[n] = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(fields[n]), values[n]);
        gtk_entry_set_activates_default(GTK_ENTRY(fields[n]), TRUE);
        gtk_box_pack_start(GTK_BOX(box), fields[n], FALSE, FALSE, 0);
    }
    gtk_widget_show_all(box);

    ok = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
    if (ok)
        for (i = 0; i < n; i++) {
            g_free(values[i]);
            values[i] = g_strdup(gtk_entry_get_text(GTK_ENTRY(fields[i])));
        }
    gtk_widget_destroy(dialog);
    return ok;
}

/* The message for a request that didn't get through or was refused. */
static char *Reason(int status, const char *text, const char *error)
{
    return status == 0 ? g_strdup(error) : ReasonForStatus(status, text);
}

/* ---------- Window state ---------- */

static void SetEntryVersion(const char *version)
{
    g_free(g.entryVersion);
    g.entryVersion = g_strdup(version);
}

static void SetSearch(const char *text)
{
    g_free(g.search);
    g.search = g_strdup(text);
}

static gboolean Changed(void)
{
    return gtk_text_buffer_get_modified(g.buffer);
}

static void UpdateTitle(void)
{
    char *title = g.server != NULL
                  ? g_strdup_printf("%sDiary \342\200\224 %s", Changed() ? "*" : "", ServerAddress(g.server))
                  : g_strdup_printf("%sDiary", Changed() ? "*" : "");

    gtk_window_set_title(GTK_WINDOW(g.window), title);
    g_free(title);
}

static void UpdateMenus(void)
{
    gtk_widget_set_sensitive(g.saveItem, Changed());
    gtk_widget_set_sensitive(g.showAllItem, g.search != NULL);
    gtk_widget_set_sensitive(g.undoItem, UndoCanUndo(g.undo));
    gtk_widget_set_sensitive(g.redoItem, UndoCanRedo(g.undo));
}

static void UndoChanged(gpointer data)
{
    UpdateMenus();
}

/* Bold days with an entry, and the open day selected, in the shown month. */
static void RefreshCalendar(void)
{
    GtkCalendar *calendar = GTK_CALENDAR(g.calendar);
    guint year, month, unused;
    int y, m, d;
    guint i;

    g.syncing = TRUE;
    gtk_calendar_get_date(calendar, &year, &month, &unused);
    gtk_calendar_clear_marks(calendar);
    for (i = 0; i < g.shownDays->len; i++) {
        DayToDate(g_array_index(g.shownDays, int, i), &y, &m, &d);
        if (y == (int)year && m == (int)month + 1)
            gtk_calendar_mark_day(calendar, d);
    }
    DayToDate(g.day, &y, &m, &d);
    gtk_calendar_select_day(calendar, y == (int)year && m == (int)month + 1 ? d : 0);
    g.syncing = FALSE;
}

/* Shows the open day's month. */
static void SyncCalendar(void)
{
    int y, m, d;

    DayToDate(g.day, &y, &m, &d);
    g.syncing = TRUE;
    gtk_calendar_select_day(GTK_CALENDAR(g.calendar), 0);
    gtk_calendar_select_month(GTK_CALENDAR(g.calendar), m - 1, y);
    g.syncing = FALSE;
    RefreshCalendar();
}

static void SyncList(void)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(g.list));
    int row = IndexOfDay(g.shownDays, g.day);

    g.syncing = TRUE;
    if (row < 0) {
        gtk_tree_selection_unselect_all(selection);
    } else {
        GtkTreePath *path = gtk_tree_path_new_from_indices(row, -1);

        gtk_tree_selection_select_path(selection, path);
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(g.list), path, NULL, FALSE, 0, 0);
        gtk_tree_path_free(path);
    }
    g.syncing = FALSE;
}

static void RefreshList(void)
{
    guint i;

    g.syncing = TRUE;
    gtk_list_store_clear(g.rows);
    for (i = 0; i < g.shownDays->len; i++) {
        int day = g_array_index(g.shownDays, int, i);
        char *label = ShortDate(day);

        gtk_list_store_insert_with_values(g.rows, NULL, -1, 0, day, 1, label, -1);
        g_free(label);
    }
    g.syncing = FALSE;
    RefreshCalendar();
    SyncList();
}

static void ApplyFont(void)
{
    char *css;

    if (g.font == NULL) {
        css = g_strdup("textview { }");
    } else {
        PangoFontMask set = pango_font_description_get_set_fields(g.font);
        GString *rule = g_string_new("textview {");
        char size[G_ASCII_DTOSTR_BUF_SIZE];

        if (set & PANGO_FONT_MASK_FAMILY)
            g_string_append_printf(rule, " font-family: \"%s\";", pango_font_description_get_family(g.font));
        if (set & PANGO_FONT_MASK_SIZE) {
            g_ascii_formatd(size, sizeof(size), "%.1f",
                            (double)pango_font_description_get_size(g.font) / PANGO_SCALE);
            g_string_append_printf(rule, " font-size: %s%s;", size,
                                   pango_font_description_get_size_is_absolute(g.font) ? "px" : "pt");
        }
        if (set & PANGO_FONT_MASK_WEIGHT)
            g_string_append_printf(rule, " font-weight: %d;", (int)pango_font_description_get_weight(g.font));
        if (set & PANGO_FONT_MASK_STYLE)
            g_string_append_printf(rule, " font-style: %s;",
                                   pango_font_description_get_style(g.font) == PANGO_STYLE_ITALIC ? "italic"
                                   : pango_font_description_get_style(g.font) == PANGO_STYLE_OBLIQUE ? "oblique"
                                   : "normal");
        g_string_append(rule, " }");
        css = g_string_free(rule, FALSE);
    }
    gtk_css_provider_load_from_data(g.fontStyle, css, -1, NULL);
    g_free(css);
}

/* ---------- Reading and saving ---------- */

static void UseServer(void)
{
    ServerFree(g.server);
    g.server = *g.address != 0 ? ServerNew(g.address, g.key) : NULL;
    UpdateTitle();
}

/* Reads the list of entries, from the server if one is set. If it can't be
   reached, Diary can work with the local folder for this session. */
static void ScanEntries(void)
{
    if (g.allDays != NULL)
        g_array_free(g.allDays, TRUE);
    g.allDays = NULL;
    while (g.server != NULL && g.allDays == NULL) {
        char *text, *error, *reason;
        int status = ServerRequest(g.server, "GET", "/entries", NULL, NULL, &text, NULL, &error);

        if (status == 200) {
            g.allDays = DaysInReply(text);
        } else {
            reason = Reason(status, text, error);
            if (!Ask("Diary couldn't get the entries from the Diary Server.", reason,
                     "Work Locally", "Retry")) {
                ServerFree(g.server);       /* for this session; the setting stays */
                g.server = NULL;
                UpdateTitle();
            }
            g_free(reason);
        }
        g_free(text);
        g_free(error);
    }
    if (g.allDays == NULL)
        g.allDays = StoreDays(g.folder);
}

static char *TextOfDay(int day)
{
    char path[32], name[11], *text, *version, *error, *reason;
    int status;

    if (g.server == NULL)
        return StoreRead(g.folder, day);

    EntryName(day, name);
    g_snprintf(path, sizeof(path), "/entries/%s", name);
    status = ServerRequest(g.server, "GET", path, NULL, NULL, &text, &version, &error);
    if (day == g.day)
        SetEntryVersion(status == 200 && version != NULL ? version : "none");
    g_free(version);
    if (status != 200 && status != 404) {
        reason = Reason(status, text, error);
        ShowMessage("Diary couldn't load the entry from the Diary Server.", reason);
        g_free(reason);
    }
    g_free(error);
    if (status != 200) {
        g_free(text);
        return NULL;
    }
    return text;
}

static void LoadEntry(void)
{
    char *text = TextOfDay(g.day), *date, *markup;
    GtkTextIter end;

    gtk_text_buffer_set_text(g.buffer, text != NULL ? text : "", -1);
    g_free(text);
    gtk_text_buffer_get_end_iter(g.buffer, &end);
    gtk_text_buffer_place_cursor(g.buffer, &end);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(g.editor), gtk_text_buffer_get_insert(g.buffer),
                                 0, FALSE, 0, 0);
    UndoClear(g.undo);
    gtk_text_buffer_set_modified(g.buffer, FALSE);

    date = LongDate(g.day);
    markup = g_markup_printf_escaped("<b>%s</b>", date);
    gtk_label_set_markup(GTK_LABEL(g.dateLabel), markup);
    g_free(markup);
    g_free(date);
    SyncCalendar();
    SyncList();
    UpdateTitle();
    UpdateMenus();
}

/* Saves (or, if text is empty, deletes) the open entry on the server. If it
   was changed on another computer since it was loaded, asks first. */
static gboolean SaveOnServer(const char *text)
{
    char path[32], name[11], *base = g_strdup(g.entryVersion);

    EntryName(g.day, name);
    g_snprintf(path, sizeof(path), "/entries/%s", name);
    for (;;) {
        char *reply, *version, *error, *reason, *message;
        int status = ServerRequest(g.server, *text == 0 ? "DELETE" : "PUT", path,
                                   *text == 0 ? NULL : text, base, &reply, &version, &error);

        if (status == 200)
            SetEntryVersion(version != NULL ? version : "none");
        if (status != 200 && status != 409) {
            reason = Reason(status, reply, error);
            message = g_strconcat(reason, "\n\nYour text is still in the window.", NULL);
            ShowMessage(status == 0 ? "Diary couldn't save the entry on the Diary Server."
                                    : "The Diary Server refused to save the entry.", message);
            g_free(message);
            g_free(reason);
        }
        g_free(reply);
        g_free(version);
        g_free(error);
        if (status != 409) {
            g_free(base);
            return status == 200;
        }

        if (!Ask("This entry was changed on another computer since you opened it.",
                 "Save your version over it?", "Keep Editing", "Overwrite")) {
            g_free(base);
            return FALSE;
        }
        g_free(base);
        base = NULL;                /* replace it unconditionally */
    }
}

/* Saves the open entry if it was changed. */
static gboolean SaveIfNeeded(void)
{
    GtkTextIter start, end;
    char *text, *message;
    gsize length;
    gboolean ok;

    if (!Changed())
        return TRUE;
    gtk_text_buffer_get_bounds(g.buffer, &start, &end);
    text = gtk_text_buffer_get_text(g.buffer, &start, &end, FALSE);
    length = strlen(text);
    while (length > 0 && text[length - 1] == '\n')
        text[--length] = 0;

    if (g.server != NULL) {
        ok = SaveOnServer(text);
    } else {
        ok = StoreWrite(g.folder, g.day, text);
        if (!ok) {
            message = g_strdup_printf("Check that the folder \342\200\234%s\342\200\235 can be written to.",
                                      g.folder);
            ShowMessage("Diary couldn't save the entry.", message);
            g_free(message);
        }
    }
    if (!ok) {
        g_free(text);
        return FALSE;
    }

    if (*text == 0) {
        RemoveDay(g.allDays, g.day);
        RemoveDay(g.shownDays, g.day);
        RefreshList();
    } else if (!ContainsDay(g.allDays, g.day)) {
        InsertDay(g.allDays, g.day);
        if (g.search == NULL)
            InsertDay(g.shownDays, g.day);
        RefreshList();
    }
    g_free(text);
    gtk_text_buffer_set_modified(g.buffer, FALSE);
    SyncList();
    return TRUE;
}

static void GoToDay(int day)
{
    if (day == g.day || !SaveIfNeeded()) {
        SyncCalendar();
        SyncList();
        return;
    }
    g.day = day;
    LoadEntry();
}

/* ---------- Searching ---------- */

static gboolean TextContains(const char *text, const char *search)
{
    char *a, *b;
    gboolean found;

    if (text == NULL)
        return FALSE;
    a = g_utf8_casefold(text, -1);
    b = g_utf8_casefold(search, -1);
    found = strstr(a, b) != NULL;
    g_free(a);
    g_free(b);
    return found;
}

static gboolean EntryContainsSearch(int day)
{
    char *text;
    gboolean found;

    /* With a server, the listed days are its search result. */
    if (g.server != NULL)
        return ContainsDay(g.shownDays, day);
    text = StoreRead(g.folder, day);
    found = TextContains(text, g.search);
    g_free(text);
    return found;
}

/* Selects the first match in the open entry, or the next one after the selection. */
static gboolean SelectMatch(gboolean fromTop)
{
    GtkTextIter from, matchStart, matchEnd;

    if (g.search == NULL)
        return FALSE;
    if (fromTop)
        gtk_text_buffer_get_start_iter(g.buffer, &from);
    else
        gtk_text_buffer_get_selection_bounds(g.buffer, NULL, &from);
    if (!gtk_text_iter_forward_search(&from, g.search,
                                      GTK_TEXT_SEARCH_CASE_INSENSITIVE | GTK_TEXT_SEARCH_TEXT_ONLY,
                                      &matchStart, &matchEnd, NULL))
        return FALSE;
    gtk_text_buffer_select_range(g.buffer, &matchStart, &matchEnd);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(g.editor), gtk_text_buffer_get_insert(g.buffer),
                                 0.1, FALSE, 0, 0);
    gtk_widget_grab_focus(g.editor);
    return TRUE;
}

static void OpenDay(int day)
{
    GoToDay(day);
    if (g.search != NULL && day == g.day)
        SelectMatch(TRUE);
}

static void ShowAllEntries(void)
{
    SetSearch(NULL);
    if (g.shownDays != NULL)
        g_array_free(g.shownDays, TRUE);
    g.shownDays = NewDays();
    g_array_append_vals(g.shownDays, g.allDays->data, g.allDays->len);
    RefreshList();
    UpdateMenus();
}

static void Find(void)
{
    static const char *const labels[] = { "Find entries containing:", NULL };
    char *values[1] = { g_strdup(g.search != NULL ? g.search : "") };
    GArray *matches;
    guint i;

    if (!AskText("Find", labels, values) || !SaveIfNeeded()) {
        g_free(values[0]);
        return;
    }
    if (*values[0] == 0) {
        g_free(values[0]);
        ShowAllEntries();
        return;
    }

    if (g.server != NULL) {
        char *query = g_uri_escape_string(values[0], NULL, FALSE);
        char *path = g_strconcat("/search?q=", query, NULL), *reply, *error, *reason;
        int status = ServerRequest(g.server, "GET", path, NULL, NULL, &reply, NULL, &error);

        g_free(path);
        g_free(query);
        matches = status == 200 ? DaysInReply(reply) : NULL;
        if (status != 200) {
            reason = Reason(status, reply, error);
            ShowMessage(status == 0 ? "Diary couldn't search on the Diary Server."
                                    : "The Diary Server refused the search.", reason);
            g_free(reason);
        }
        g_free(reply);
        g_free(error);
        if (matches == NULL) {
            g_free(values[0]);
            return;
        }
    } else {
        matches = NewDays();
        for (i = 0; i < g.allDays->len; i++) {
            int day = g_array_index(g.allDays, int, i);
            char *text = StoreRead(g.folder, day);

            if (TextContains(text, values[0]))
                g_array_append_val(matches, day);
            g_free(text);
        }
    }

    if (matches->len == 0) {
        ShowMessage("No entries contain that text.", NULL);    /* keep the current filter */
        g_array_free(matches, TRUE);
        g_free(values[0]);
        return;
    }
    SetSearch(values[0]);
    g_free(values[0]);
    g_array_free(g.shownDays, TRUE);
    g.shownDays = matches;
    RefreshList();
    UpdateMenus();
    if (!ContainsDay(g.shownDays, g.day))
        GoToDay(g_array_index(g.shownDays, int, 0));
    SyncList();
    SelectMatch(TRUE);
}

/* Moves to the next match: first within the open entry, then on to the
   following (older) matching entries, wrapping around to the newest. */
static void FindAgain(void)
{
    int count, start, step;

    if (g.search == NULL) {
        Find();
        return;
    }
    if (SelectMatch(FALSE))
        return;

    count = g.shownDays->len;
    start = IndexOfDay(g.shownDays, g.day);
    for (step = 1; step <= count; step++) {
        int i = start < 0 ? step - 1 : (start + step) % count;
        int day = g_array_index(g.shownDays, int, i);

        if (day == g.day) {
            if (SelectMatch(TRUE))
                return;
        } else if (EntryContainsSearch(day)) {
            OpenDay(day);
            return;
        }
    }
    gtk_widget_error_bell(g.window);
}

/* ---------- Commands ---------- */

static void Quit(void)
{
    if (!SaveIfNeeded() && !Ask("The entry could not be saved.", "Quit anyway?", "Cancel", "Quit"))
        return;
    gtk_window_get_size(GTK_WINDOW(g.window), &g.width, &g.height);
    SaveSettings();
    gtk_main_quit();
}

/* File > Diary Server...: where the entries are kept. */
static void ChooseServer(void)
{
    static const char *const labels[] = {
        "Keep the entries on a Diary Server at this address (host or host:port; "
        "leave it empty for the \342\200\234Diary Entries\342\200\235 folder):",
        "The server\342\200\231s key (leave it empty if it has none):", NULL
    };
    char *values[2] = { g_strdup(g.address), g_strdup(g.key) };

    if (!AskText("Diary Server", labels, values) || !SaveIfNeeded()) {
        g_free(values[0]);
        g_free(values[1]);
        return;
    }
    g_free(g.address);
    g_free(g.key);
    g.address = g_strstrip(values[0]);
    g.key = *g.address != 0 ? values[1] : (g_free(values[1]), g_strdup(""));
    SaveSettings();

    UseServer();
    ScanEntries();
    ShowAllEntries();
    LoadEntry();
}

static void ChooseFont(void)
{
    GtkWidget *dialog = gtk_font_chooser_dialog_new("Font", GTK_WINDOW(g.window));

    if (g.font != NULL)
        gtk_font_chooser_set_font_desc(GTK_FONT_CHOOSER(dialog), g.font);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        if (g.font != NULL)
            pango_font_description_free(g.font);
        g.font = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(dialog));
        ApplyFont();
    }
    gtk_widget_destroy(dialog);
}

/* Bigger and Smaller: one point at a time. */
static void ChangeFontSize(int delta)
{
    int size;

    if (g.font == NULL)
        g.font = pango_font_description_copy(
            pango_context_get_font_description(gtk_widget_get_pango_context(g.editor)));
    size = pango_font_description_get_size(g.font) / PANGO_SCALE;
    if (size <= 0)
        size = 10;
    size = CLAMP(size + delta, 6, 72);
    pango_font_description_set_size(g.font, size * PANGO_SCALE);
    ApplyFont();
}

static void About(void)
{
    gtk_show_about_dialog(GTK_WINDOW(g.window),
                          "program-name", "Diary",
                          "version", kVersion,
                          "comments", "A simple one-page-per-day journal.",
                          "website", kWebsite,
                          "logo-icon-name", "diary",
                          "license", "No rights reserved: Diary is dedicated to the public domain "
                                     "under CC0 1.0. Use it for anything, no attribution needed.",
                          "wrap-license", TRUE,
                          NULL);
}

/* Cut, Copy, Paste and Select All go to the text field that has the focus. */
static void EditCommand(const char *signal)
{
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(g.window));

    if (GTK_IS_TEXT_VIEW(focus) || GTK_IS_ENTRY(focus)) {
        if (strcmp(signal, "select-all") != 0)
            g_signal_emit_by_name(focus, signal);
        else if (GTK_IS_TEXT_VIEW(focus))
            g_signal_emit_by_name(focus, signal, TRUE);
        else
            gtk_editable_select_region(GTK_EDITABLE(focus), 0, -1);
    }
}

static void ScrollToCursor(void)
{
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(g.editor), gtk_text_buffer_get_insert(g.buffer));
}

enum {
    kSave, kServer, kQuit, kUndo, kRedo, kCut, kCopy, kPaste, kSelectAll,
    kFind, kFindAgain, kShowAll, kPrevious, kNext, kToday, kFont, kBigger, kSmaller, kAbout
};

static void OnCommand(GtkMenuItem *item, gpointer data)
{
    switch (GPOINTER_TO_INT(data)) {
    case kSave:      SaveIfNeeded(); break;
    case kServer:    ChooseServer(); break;
    case kQuit:      Quit(); break;
    case kUndo:      UndoUndo(g.undo); ScrollToCursor(); break;
    case kRedo:      UndoRedo(g.undo); ScrollToCursor(); break;
    case kCut:       EditCommand("cut-clipboard"); break;
    case kCopy:      EditCommand("copy-clipboard"); break;
    case kPaste:     EditCommand("paste-clipboard"); break;
    case kSelectAll: EditCommand("select-all"); break;
    case kFind:      Find(); break;
    case kFindAgain: FindAgain(); break;
    case kShowAll:   ShowAllEntries(); break;
    case kPrevious:  GoToDay(g.day - 1); break;
    case kNext:      GoToDay(g.day + 1); break;
    case kToday:     GoToDay(Today()); break;
    case kFont:      ChooseFont(); break;
    case kBigger:    ChangeFontSize(1); break;
    case kSmaller:   ChangeFontSize(-1); break;
    case kAbout:     About(); break;
    }
}

/* ---------- Window ---------- */

static GtkWidget *AddMenu(GtkWidget *bar, const char *title)
{
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(title), *menu = gtk_menu_new();

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
    return menu;
}

/* An item; with shown set, the key is only shown, as the text field handles it. */
static GtkWidget *AddItem(GtkWidget *menu, GtkAccelGroup *keys, const char *title, int command,
                          guint key, GdkModifierType modifiers, gboolean shown)
{
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(title);

    g_signal_connect(item, "activate", G_CALLBACK(OnCommand), GINT_TO_POINTER(command));
    if (key != 0 && shown)
        gtk_accel_label_set_accel(GTK_ACCEL_LABEL(gtk_bin_get_child(GTK_BIN(item))), key, modifiers);
    else if (key != 0)
        gtk_widget_add_accelerator(item, "activate", keys, key, modifiers, GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

static void AddSeparator(GtkWidget *menu)
{
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
}

static GtkWidget *CreateMenus(GtkAccelGroup *keys)
{
    GtkWidget *bar = gtk_menu_bar_new(), *menu, *item;

    menu = AddMenu(bar, "_File");
    g.saveItem = AddItem(menu, keys, "_Save", kSave, GDK_KEY_s, GDK_CONTROL_MASK, FALSE);
    AddSeparator(menu);
    AddItem(menu, keys, "Diary _Server\342\200\246", kServer, 0, 0, FALSE);
    AddSeparator(menu);
    AddItem(menu, keys, "_Quit", kQuit, GDK_KEY_q, GDK_CONTROL_MASK, FALSE);

    menu = AddMenu(bar, "_Edit");
    g.undoItem = AddItem(menu, keys, "_Undo", kUndo, GDK_KEY_z, GDK_CONTROL_MASK, FALSE);
    g.redoItem = AddItem(menu, keys, "_Redo", kRedo, GDK_KEY_z, GDK_CONTROL_MASK | GDK_SHIFT_MASK, FALSE);
    gtk_widget_add_accelerator(g.redoItem, "activate", keys, GDK_KEY_y, GDK_CONTROL_MASK, 0);
    AddSeparator(menu);
    AddItem(menu, keys, "Cu_t", kCut, GDK_KEY_x, GDK_CONTROL_MASK, TRUE);
    AddItem(menu, keys, "_Copy", kCopy, GDK_KEY_c, GDK_CONTROL_MASK, TRUE);
    AddItem(menu, keys, "_Paste", kPaste, GDK_KEY_v, GDK_CONTROL_MASK, TRUE);
    AddItem(menu, keys, "Select _All", kSelectAll, GDK_KEY_a, GDK_CONTROL_MASK, TRUE);
    AddSeparator(menu);
    AddItem(menu, keys, "_Find\342\200\246", kFind, GDK_KEY_f, GDK_CONTROL_MASK, FALSE);
    AddItem(menu, keys, "Find _Again", kFindAgain, GDK_KEY_g, GDK_CONTROL_MASK, FALSE);
    g.showAllItem = AddItem(menu, keys, "Show All _Entries", kShowAll, 0, 0, FALSE);

    menu = AddMenu(bar, "_Go");
    AddItem(menu, keys, "_Previous Day", kPrevious, GDK_KEY_Left, GDK_MOD1_MASK, FALSE);
    AddItem(menu, keys, "_Next Day", kNext, GDK_KEY_Right, GDK_MOD1_MASK, FALSE);
    AddItem(menu, keys, "_Today", kToday, GDK_KEY_t, GDK_CONTROL_MASK, FALSE);

    /* The entry is plain text, so a font applies to all of it. */
    menu = AddMenu(bar, "F_ormat");
    AddItem(menu, keys, "_Font\342\200\246", kFont, 0, 0, FALSE);
    item = AddItem(menu, keys, "_Bigger", kBigger, GDK_KEY_plus, GDK_CONTROL_MASK, FALSE);
    gtk_widget_add_accelerator(item, "activate", keys, GDK_KEY_equal, GDK_CONTROL_MASK, 0);
    gtk_widget_add_accelerator(item, "activate", keys, GDK_KEY_KP_Add, GDK_CONTROL_MASK, 0);
    item = AddItem(menu, keys, "_Smaller", kSmaller, GDK_KEY_minus, GDK_CONTROL_MASK, FALSE);
    gtk_widget_add_accelerator(item, "activate", keys, GDK_KEY_KP_Subtract, GDK_CONTROL_MASK, 0);

    menu = AddMenu(bar, "_Help");
    AddItem(menu, keys, "_About Diary", kAbout, 0, 0, FALSE);
    return bar;
}

static gboolean CalendarIdle(gpointer data)
{
    g.calendarIdle = 0;
    g.skipDaySelected = FALSE;
    RefreshCalendar();
    return G_SOURCE_REMOVE;
}

/* GtkCalendar selects a day in every month it turns to; that shouldn't
   open it, so the selection it makes then is skipped. */
static void OnMonthChanged(GtkCalendar *calendar, gpointer data)
{
    if (g.syncing)
        return;
    g.skipDaySelected = TRUE;
    if (g.calendarIdle == 0)
        g.calendarIdle = g_idle_add(CalendarIdle, NULL);
}

static gboolean MouseButtonDown(void)
{
    GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(g.window));
    GdkModifierType state = 0;

    gdk_window_get_device_position(gtk_widget_get_window(g.window), gdk_seat_get_pointer(seat),
                                   NULL, NULL, &state);
    return (state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)) != 0;
}

static gboolean OpenPendingDay(gpointer data)
{
    /* Wait for the click to end, so a question about saving doesn't take
       the list's or calendar's mouse button away from it. */
    if (MouseButtonDown())
        return G_SOURCE_CONTINUE;
    g.openTimer = 0;
    OpenDay(g.pendingDay);
    return G_SOURCE_REMOVE;
}

/* Opens a day picked in the calendar or list once the click is over. */
static void OpenDayLater(int day)
{
    g.pendingDay = day;
    if (g.openTimer == 0)
        g.openTimer = g_timeout_add(20, OpenPendingDay, NULL);
}

static void OnDaySelected(GtkCalendar *calendar, gpointer data)
{
    guint year, month, day;

    if (g.syncing)
        return;
    if (g.skipDaySelected) {
        g.skipDaySelected = FALSE;
        return;
    }
    gtk_calendar_get_date(calendar, &year, &month, &day);
    if (day != 0)
        OpenDayLater(DateToDay(year, month + 1, day));
}

static void OnListSelection(GtkTreeSelection *selection, gpointer data)
{
    GtkTreeModel *model;
    GtkTreeIter row;
    int day;

    if (g.syncing || !gtk_tree_selection_get_selected(selection, &model, &row))
        return;
    gtk_tree_model_get(model, &row, 0, &day, -1);
    OpenDayLater(day);
}

static void OnModifiedChanged(GtkTextBuffer *buffer, gpointer data)
{
    UpdateTitle();
    UpdateMenus();
}

static gboolean OnDelete(GtkWidget *window, GdkEvent *event, gpointer data)
{
    Quit();
    return TRUE;                    /* Quit ends the program if it should */
}

static void OnButton(GtkButton *button, gpointer data)
{
    OnCommand(NULL, data);
}

static GtkWidget *Button(const char *title, int command)
{
    GtkWidget *button = gtk_button_new_with_label(title);

    gtk_widget_set_can_focus(button, FALSE);
    g_signal_connect(button, "clicked", G_CALLBACK(OnButton), GINT_TO_POINTER(command));
    return button;
}

static void CreateWindow(void)
{
    GtkAccelGroup *keys = gtk_accel_group_new();
    GtkWidget *outer, *columns, *left, *right, *row, *scroller;
    GtkCellRenderer *renderer;

    g.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(g.window), g.width, g.height);
    gtk_window_add_accel_group(GTK_WINDOW(g.window), keys);
    g_signal_connect(g.window, "delete-event", G_CALLBACK(OnDelete), NULL);

    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(g.window), outer);
    gtk_box_pack_start(GTK_BOX(outer), CreateMenus(keys), FALSE, FALSE, 0);

    columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, kMargin);
    gtk_container_set_border_width(GTK_CONTAINER(columns), kMargin);
    gtk_box_pack_start(GTK_BOX(outer), columns, TRUE, TRUE, 0);

    /* Left: the calendar and the entry list */
    left = gtk_box_new(GTK_ORIENTATION_VERTICAL, kMargin);
    gtk_widget_set_size_request(left, kListWidth, -1);
    gtk_box_pack_start(GTK_BOX(columns), left, FALSE, FALSE, 0);

    g.calendar = gtk_calendar_new();
    g_signal_connect(g.calendar, "month-changed", G_CALLBACK(OnMonthChanged), NULL);
    g_signal_connect(g.calendar, "day-selected", G_CALLBACK(OnDaySelected), NULL);
    gtk_box_pack_start(GTK_BOX(left), g.calendar, FALSE, FALSE, 0);

    g.rows = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
    g.list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g.rows));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(g.list), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(g.list), FALSE);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(g.list),
                                gtk_tree_view_column_new_with_attributes("Day", renderer, "text", 1, NULL));
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(g.list)), "changed",
                     G_CALLBACK(OnListSelection), NULL);
    scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroller), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroller), g.list);
    gtk_box_pack_start(GTK_BOX(left), scroller, TRUE, TRUE, 0);

    /* Right: the day's buttons and date, then its text */
    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(columns), right, TRUE, TRUE, 0);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(right), row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), Button("<", kPrevious), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), Button("Today", kToday), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), Button(">", kNext), FALSE, FALSE, 0);
    g.dateLabel = gtk_label_new(NULL);
    gtk_widget_set_margin_start(g.dateLabel, 8);
    gtk_label_set_xalign(GTK_LABEL(g.dateLabel), 0);
    gtk_label_set_ellipsize(GTK_LABEL(g.dateLabel), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(row), g.dateLabel, TRUE, TRUE, 0);

    g.editor = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g.editor), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(g.editor), 6);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(g.editor), 6);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(g.editor), 4);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(g.editor), 4);
    g.buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g.editor));
    g.undo = UndoNew(g.buffer, UndoChanged, NULL);
    g_signal_connect(g.buffer, "modified-changed", G_CALLBACK(OnModifiedChanged), NULL);
    g.fontStyle = gtk_css_provider_new();
    gtk_style_context_add_provider(gtk_widget_get_style_context(g.editor),
                                   GTK_STYLE_PROVIDER(g.fontStyle),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    ApplyFont();
    scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroller), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroller), g.editor);
    gtk_box_pack_start(GTK_BOX(right), scroller, TRUE, TRUE, 0);
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
    g_set_application_name("Diary");
    gtk_window_set_default_icon_name("diary");

    LoadSettings();
    g.folder = StoreDefaultFolder();
    g.day = Today();
    g.shownDays = NewDays();
    CreateWindow();
    UseServer();
    gtk_widget_show_all(g.window);
    gtk_widget_grab_focus(g.editor);

    ScanEntries();
    ShowAllEntries();
    LoadEntry();
    gtk_main();
    return 0;
}
