/* Dates as day numbers (days since 1 January 1970), as in the other Diary
   versions, plus the English names they use. */

#ifndef DATES_H
#define DATES_H

#include <glib.h>

void DayToDate(int day, int *year, int *month, int *dayOfMonth);
int DateToDay(int year, int month, int dayOfMonth);
int Today(void);
int Weekday(int day);                       /* 0 = Sunday */

void EntryName(int day, char name[11]);     /* YYYY-MM-DD */
gboolean ParseEntryName(const char *name, int *day);

char *LongDate(int day);                    /* "Thursday, 5 November 2026" */
char *ShortDate(int day);                   /* "Thu 05 Nov 2026" */

#endif
