/* The entries as plain text files named YYYY-MM-DD in a folder, and the
   lists of days Diary keeps (GArrays of int, newest first). */

#ifndef STORE_H
#define STORE_H

#include <glib.h>

char *StoreDefaultFolder(void);
GArray *StoreDays(const char *folder);
char *StoreRead(const char *folder, int day);       /* NULL if there's none */
gboolean StoreWrite(const char *folder, int day, const char *text);

char *DecodeText(const char *data, gsize length);   /* to UTF-8 with LF line ends */

GArray *NewDays(void);
gboolean ContainsDay(GArray *days, int day);
int IndexOfDay(GArray *days, int day);              /* -1 if it isn't there */
void InsertDay(GArray *days, int day);
void RemoveDay(GArray *days, int day);

#endif
