#include <errno.h>
#include <string.h>
#include <glib/gstdio.h>

#include "dates.h"
#include "store.h"

/* Documents/Diary Entries, like the Mac OS X version. */
char *StoreDefaultFolder(void)
{
    const char *documents = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);

    if (documents != NULL)
        return g_build_filename(documents, "Diary Entries", NULL);
    return g_build_filename(g_get_home_dir(), "Documents", "Diary Entries", NULL);
}

/* UTF-8 as it is; otherwise the classic Mac version's MacRoman (CR line
   ends, and only MacRoman uses 0x80..0x9F) or the Amiga's ISO Latin-1. */
char *DecodeText(const char *data, gsize length)
{
    gboolean hasLF = FALSE, macOnly = FALSE;
    char *text, *in, *out;
    gsize i;

    if (g_utf8_validate(data, length, NULL)) {
        text = g_strndup(data, length);
    } else {
        for (i = 0; i < length; i++) {
            unsigned char c = data[i];
            if (c == '\n')
                hasLF = TRUE;
            else if (c >= 0x80 && c < 0xA0)
                macOnly = TRUE;
        }
        text = g_convert(data, length, "UTF-8", (!hasLF || macOnly) ? "MACINTOSH" : "ISO-8859-1",
                         NULL, NULL, NULL);
        if (text == NULL)
            text = g_convert(data, length, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
        if (text == NULL)
            text = g_strdup("");
    }

    /* CR LF and CR become LF. */
    for (in = out = text; *in != 0; in++) {
        if (*in == '\r') {
            *out++ = '\n';
            if (in[1] == '\n')
                in++;
        } else {
            *out++ = *in;
        }
    }
    *out = 0;
    return text;
}

static gint NewestFirst(gconstpointer a, gconstpointer b)
{
    int x = *(const int *)a, y = *(const int *)b;

    return x < y ? 1 : x > y ? -1 : 0;
}

GArray *StoreDays(const char *folder)
{
    GArray *days = NewDays();
    GDir *dir = g_dir_open(folder, 0, NULL);
    const char *name;
    int day;

    if (dir == NULL)
        return days;
    while ((name = g_dir_read_name(dir)) != NULL)
        if (ParseEntryName(name, &day))
            g_array_append_val(days, day);
    g_dir_close(dir);
    g_array_sort(days, NewestFirst);
    return days;
}

static char *PathOfDay(const char *folder, int day)
{
    char name[11];

    EntryName(day, name);
    return g_build_filename(folder, name, NULL);
}

char *StoreRead(const char *folder, int day)
{
    char *path = PathOfDay(folder, day), *data, *text;
    gsize length, end;

    if (!g_file_get_contents(path, &data, &length, NULL)) {
        g_free(path);
        return NULL;
    }
    g_free(path);
    text = DecodeText(data, length);
    g_free(data);
    end = strlen(text);
    if (end > 0 && text[end - 1] == '\n')
        text[end - 1] = 0;          /* StoreWrite adds one */
    return text;
}

/* An emptied entry is removed rather than kept as an empty file. */
gboolean StoreWrite(const char *folder, int day, const char *text)
{
    char *path = PathOfDay(folder, day), *data;
    gboolean ok;

    if (*text == 0) {
        ok = g_remove(path) == 0 || errno == ENOENT;
    } else if (g_mkdir_with_parents(folder, 0755) != 0) {
        ok = FALSE;
    } else {
        data = g_strconcat(text, "\n", NULL);
        ok = g_file_set_contents(path, data, -1, NULL);
        g_free(data);
    }
    g_free(path);
    return ok;
}

GArray *NewDays(void)
{
    return g_array_new(FALSE, FALSE, sizeof(int));
}

int IndexOfDay(GArray *days, int day)
{
    guint i;

    if (days != NULL)
        for (i = 0; i < days->len; i++)
            if (g_array_index(days, int, i) == day)
                return i;
    return -1;
}

gboolean ContainsDay(GArray *days, int day)
{
    return IndexOfDay(days, day) >= 0;
}

/* Keeps the list newest first. */
void InsertDay(GArray *days, int day)
{
    guint i;

    if (days == NULL || ContainsDay(days, day))
        return;
    for (i = 0; i < days->len; i++)
        if (g_array_index(days, int, i) < day)
            break;
    g_array_insert_val(days, i, day);
}

void RemoveDay(GArray *days, int day)
{
    int i = IndexOfDay(days, day);

    if (i >= 0)
        g_array_remove_index(days, i);
}
