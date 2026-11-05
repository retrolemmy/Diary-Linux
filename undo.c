#include <string.h>

#include "undo.h"

typedef struct {
    gboolean insert;        /* else a deletion */
    int offset, chars;      /* in characters */
    GString *text;
    guint group;            /* edits of one user action are undone together */
} Edit;

struct Undo {
    GtkTextBuffer *buffer;
    GPtrArray *undo, *redo; /* of Edit, the latest last */
    guint group, groups;
    int depth;              /* nesting of user actions */
    gboolean applying;
    void (*changed)(gpointer);
    gpointer data;
};

static void FreeEdit(gpointer pointer)
{
    Edit *edit = pointer;

    g_string_free(edit->text, TRUE);
    g_free(edit);
}

static Edit *Last(GPtrArray *edits)
{
    return edits->len > 0 ? g_ptr_array_index(edits, edits->len - 1) : NULL;
}

static void Record(Undo *undo, gboolean insert, int offset, const char *text, int length)
{
    int chars = g_utf8_strlen(text, length);
    Edit *last = Last(undo->undo), *edit;
    guint group = undo->depth > 0 ? undo->group : ++undo->groups;

    if (undo->applying || chars == 0)
        return;
    if (undo->redo->len > 0)
        g_ptr_array_set_size(undo->redo, 0);

    /* Typing and deleting one character at a time joins the previous edit,
       but a space after a word starts a new one. */
    if (last != NULL && chars == 1 && text[0] != '\n' && last->insert == insert) {
        gunichar c = g_utf8_get_char(text);
        gunichar end = g_utf8_get_char(g_utf8_prev_char(last->text->str + last->text->len));
        gboolean merge = FALSE;

        if (insert && offset == last->offset + last->chars
                && !(g_unichar_isspace(c) && !g_unichar_isspace(end))) {
            g_string_append_len(last->text, text, length);
            merge = TRUE;
        } else if (!insert && offset + 1 == last->offset) {     /* Backspace */
            g_string_prepend_len(last->text, text, length);
            last->offset = offset;
            merge = TRUE;
        } else if (!insert && offset == last->offset) {         /* Delete */
            g_string_append_len(last->text, text, length);
            merge = TRUE;
        }
        if (merge) {
            last->chars++;
            if (undo->depth > 0)
                undo->group = last->group;
            return;
        }
    }

    edit = g_new(Edit, 1);
    edit->insert = insert;
    edit->offset = offset;
    edit->chars = chars;
    edit->text = g_string_new_len(text, length);
    edit->group = group;
    g_ptr_array_add(undo->undo, edit);
    if (undo->changed != NULL)
        undo->changed(undo->data);
}

static void OnInsert(GtkTextBuffer *buffer, GtkTextIter *where, char *text, int length, gpointer data)
{
    Record(data, TRUE, gtk_text_iter_get_offset(where), text, length);
}

static void OnDelete(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, gpointer data)
{
    GtkTextIter from = *start, to = *end;
    char *text;

    gtk_text_iter_order(&from, &to);
    text = gtk_text_buffer_get_text(buffer, &from, &to, TRUE);
    Record(data, FALSE, gtk_text_iter_get_offset(&from), text, strlen(text));
    g_free(text);
}

static void OnBegin(GtkTextBuffer *buffer, gpointer data)
{
    Undo *undo = data;

    if (undo->depth++ == 0)
        undo->group = ++undo->groups;
}

static void OnEnd(GtkTextBuffer *buffer, gpointer data)
{
    Undo *undo = data;

    if (undo->depth > 0)
        undo->depth--;
}

Undo *UndoNew(GtkTextBuffer *buffer, void (*changed)(gpointer), gpointer data)
{
    Undo *undo = g_new0(Undo, 1);

    undo->buffer = buffer;
    undo->undo = g_ptr_array_new_with_free_func(FreeEdit);
    undo->redo = g_ptr_array_new_with_free_func(FreeEdit);
    undo->changed = changed;
    undo->data = data;
    g_signal_connect(buffer, "insert-text", G_CALLBACK(OnInsert), undo);
    g_signal_connect(buffer, "delete-range", G_CALLBACK(OnDelete), undo);
    g_signal_connect(buffer, "begin-user-action", G_CALLBACK(OnBegin), undo);
    g_signal_connect(buffer, "end-user-action", G_CALLBACK(OnEnd), undo);
    return undo;
}

void UndoClear(Undo *undo)
{
    g_ptr_array_set_size(undo->undo, 0);
    g_ptr_array_set_size(undo->redo, 0);
    if (undo->changed != NULL)
        undo->changed(undo->data);
}

gboolean UndoCanUndo(Undo *undo)
{
    return undo->undo->len > 0;
}

gboolean UndoCanRedo(Undo *undo)
{
    return undo->redo->len > 0;
}

/* Does (forward) or reverts an edit, and puts the cursor after it. */
static void Apply(Undo *undo, Edit *edit, gboolean forward)
{
    GtkTextIter start, end;

    gtk_text_buffer_get_iter_at_offset(undo->buffer, &start, edit->offset);
    if (edit->insert == forward) {
        gtk_text_buffer_insert(undo->buffer, &start, edit->text->str, edit->text->len);
        gtk_text_buffer_get_iter_at_offset(undo->buffer, &start, edit->offset + edit->chars);
    } else {
        gtk_text_buffer_get_iter_at_offset(undo->buffer, &end, edit->offset + edit->chars);
        gtk_text_buffer_delete(undo->buffer, &start, &end);
    }
    gtk_text_buffer_place_cursor(undo->buffer, &start);
}

/* Moves the latest group of edits from one list to the other. */
static void Move(Undo *undo, GPtrArray *from, GPtrArray *to, gboolean forward)
{
    Edit *edit = Last(from);
    guint group;

    if (edit == NULL)
        return;
    group = edit->group;
    undo->applying = TRUE;
    while ((edit = Last(from)) != NULL && edit->group == group) {
        Apply(undo, edit, forward);
        g_ptr_array_add(to, g_ptr_array_steal_index(from, from->len - 1));
    }
    undo->applying = FALSE;
    if (undo->changed != NULL)
        undo->changed(undo->data);
}

void UndoUndo(Undo *undo)
{
    Move(undo, undo->undo, undo->redo, FALSE);
}

void UndoRedo(Undo *undo)
{
    Move(undo, undo->redo, undo->undo, TRUE);
}
