/* time.h -- Time Functions for Zenbite C Compiler
 *
 * Provides time and date functions using the system clock.
 * Include this in .ZBX programs: #include "time.h"
 */
#ifndef ZENBITE_TIME_H
#define ZENBITE_TIME_H

/* ── Time Constants ────────────────────────────────────────────────── */

#define CLOCKS_PER_SEC  100     /* 100 Hz timer */
#define TICKS_PER_SEC   100
#define MS_PER_TICK     10
#define SECS_PER_MIN    60
#define MINS_PER_HOUR   60
#define HOURS_PER_DAY   24
#define DAYS_PER_WEEK   7
#define DAYS_PER_MONTH  30
#define DAYS_PER_YEAR   365

/* ── Time Structure ────────────────────────────────────────────────── */

typedef struct {
    int sec;        /* Seconds (0-59) */
    int min;        /* Minutes (0-59) */
    int hour;       /* Hours (0-23) */
    int day;        /* Day of month (1-31) */
    int month;      /* Month (1-12) */
    int year;       /* Year (e.g., 2024) */
    int wday;       /* Day of week (0=Sun, 6=Sat) */
} time_t;

/* ── Timer Functions ───────────────────────────────────────────────── */

/* Get current system tick count */
int clock(void);

/* Get current tick count (alias) */
int ticks(void);

/* Get elapsed milliseconds since program start */
int millis(void);

/* Get elapsed seconds since program start */
int seconds(void);

/* ── Sleep Functions ───────────────────────────────────────────────── */

/* Sleep for specified number of ticks (10ms each) */
void sleep(int ticks);

/* Sleep for specified milliseconds */
void sleep_ms(int ms);

/* Sleep for specified seconds */
void sleep_sec(int sec);

/* ── Date/Time Functions ───────────────────────────────────────────── */

/* Get current time (returns time_t structure) */
time_t get_time(void);

/* Get current time as Unix timestamp */
int time(int *t);

/* Convert Unix timestamp to time_t */
time_t mktime(time_t *tm);

/* Convert time_t to string (format: "YYYY-MM-DD HH:MM:SS") */
char *ctime(time_t *tm, char *buf);

/* Format time according to format string */
char *strftime(char *buf, int max, const char *fmt, time_t *tm);

/* Get current date as string (format: "YYYY-MM-DD") */
char *date_str(char *buf);

/* Get current time as string (format: "HH:MM:SS") */
char *time_str(char *buf);

/* ── Date Helpers ──────────────────────────────────────────────────── */

/* Check if year is a leap year */
int is_leap_year(int year);

/* Get number of days in month */
int days_in_month(int month, int year);

/* Get day of week (0=Sun, 6=Sat) */
int day_of_week(int year, int month, int day);

/* Get day of year (1-366) */
int day_of_year(int year, int month, int day);

/* ── Stopwatch ─────────────────────────────────────────────────────── */

/* Start stopwatch */
void stopwatch_start(void);

/* Stop stopwatch and return elapsed milliseconds */
int stopwatch_stop(void);

/* Get elapsed time without stopping */
int stopwatch_elapsed(void);

/* ── Timer Callbacks ───────────────────────────────────────────────── */

/* Set periodic timer (calls function every ms milliseconds) */
void timer_set(int ms, void (*callback)(void));

/* Cancel periodic timer */
void timer_cancel(void);

/* ── Constants ─────────────────────────────────────────────────────── */

/* Month constants */
#define MONTH_JAN   1
#define MONTH_FEB   2
#define MONTH_MAR   3
#define MONTH_APR   4
#define MONTH_MAY   5
#define MONTH_JUN   6
#define MONTH_JUL   7
#define MONTH_AUG   8
#define MONTH_SEP   9
#define MONTH_OCT   10
#define MONTH_NOV   11
#define MONTH_DEC   12

/* Day constants */
#define DAY_SUN     0
#define DAY_MON     1
#define DAY_TUE     2
#define DAY_WED     3
#define DAY_THU     4
#define DAY_FRI     5
#define DAY_SAT     6

#endif /* ZENBITE_TIME_H */
