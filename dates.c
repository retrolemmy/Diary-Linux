#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dates.h"

static const char *const kWeekdays[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const kMonths[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

/* Civil date from a day number (H. Hinnant's algorithm). */
void DayToDate(int day, int *year, int *month, int *dayOfMonth)
{
    int z = day + 719468;           /* days since 1 March 0000 */
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int m = mp < 10 ? mp + 3 : mp - 9;

    *year = yoe + era * 400 + (m <= 2);
    *month = m;
    *dayOfMonth = doy - (153 * mp + 2) / 5 + 1;
}

int DateToDay(int year, int month, int dayOfMonth)
{
    int y = year - (month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + dayOfMonth - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return era * 146097 + doe - 719468;
}

int Today(void)
{
    time_t now = time(NULL);
    struct tm local;

    localtime_r(&now, &local);
    return DateToDay(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

/* 1 January 1970 was a Thursday. */
int Weekday(int day)
{
    return ((day + 4) % 7 + 7) % 7;
}

void EntryName(int day, char name[11])
{
    int y, m, d;

    DayToDate(day, &y, &m, &d);
    snprintf(name, 11, "%04d-%02d-%02d", y, m, d);
}

/* Parses an entry name; rejects anything else, including dates like 31 April. */
gboolean ParseEntryName(const char *s, int *day)
{
    int y, m, d, i, cy, cm, cd;

    if (s == NULL || strlen(s) != 10)
        return FALSE;
    for (i = 0; i < 10; i++)
        if ((i == 4 || i == 7) ? s[i] != '-' : (s[i] < '0' || s[i] > '9'))
            return FALSE;
    y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    m = (s[5] - '0') * 10 + (s[6] - '0');
    d = (s[8] - '0') * 10 + (s[9] - '0');
    if (y < 1904 || m < 1 || m > 12 || d < 1 || d > 31)
        return FALSE;
    *day = DateToDay(y, m, d);
    DayToDate(*day, &cy, &cm, &cd);
    return cy == y && cm == m && cd == d;
}

char *LongDate(int day)
{
    int y, m, d;

    DayToDate(day, &y, &m, &d);
    return g_strdup_printf("%s, %d %s %d", kWeekdays[Weekday(day)], d, kMonths[m - 1], y);
}

char *ShortDate(int day)
{
    int y, m, d;

    DayToDate(day, &y, &m, &d);
    return g_strdup_printf("%.3s %02d %.3s %d", kWeekdays[Weekday(day)], d, kMonths[m - 1], y);
}
