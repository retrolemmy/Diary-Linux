/* Undo and Redo for a GtkTextBuffer, which GTK 3 doesn't have itself.
   Typing is undone a word at a time. */

#ifndef UNDO_H
#define UNDO_H

#include <gtk/gtk.h>

typedef struct Undo Undo;

/* changed is called whenever CanUndo or CanRedo may have changed. */
Undo *UndoNew(GtkTextBuffer *buffer, void (*changed)(gpointer), gpointer data);
void UndoClear(Undo *undo);
gboolean UndoCanUndo(Undo *undo);
gboolean UndoCanRedo(Undo *undo);
void UndoUndo(Undo *undo);
void UndoRedo(Undo *undo);

#endif
