#include "rtc.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include <log/log.h>

#include "util/file.h"

/**
 *  Constants
 */
#define RTC_ISO_FORMAT "%04d%02d%02dT%02d%02d%02d"
#define RTC_LOG_FORMAT "%04d-%02d-%02dT%02d:%02d:%02d"
#define RTC_OSD_FORMAT "%04d/%02d/%02d %02d:%02d:%02d"
static const char *RTC_DEV = "/dev/rtc";
static const char *RTC_FILE = "/mnt/extsd/rtc.txt";

/**
 *  Globals
 */
static int g_rtc_has_battery = 0;

/**
 *  Conversion Utils
 */
void rd2rt(const struct rtc_date *rd, struct rtc_time *rt) {
    rt->tm_year = rd->year - 1900;
    rt->tm_mon = rd->month - 1;
    rt->tm_mday = rd->day;
    rt->tm_hour = rd->hour;
    rt->tm_min = rd->min;
    rt->tm_sec = rd->sec;
}
void rt2rd(const struct rtc_time *rt, struct rtc_date *rd) {
    rd->year = rt->tm_year + 1900;
    rd->month = rt->tm_mon + 1;
    rd->day = rt->tm_mday;
    rd->hour = rt->tm_hour;
    rd->min = rt->tm_min;
    rd->sec = rt->tm_sec;
}

/**
 *  Initialize RTC from file if detected,
 *  must be formatted to ISO-8601:
 *  YYYY-MM-DDTHH:MM:SS
 *  2023-03-10T01:05:15
 */
void rtc_init() {
    struct rtc_date rd_now;
    rtc_get_clock(&rd_now);

    // Has time has accumulated since the
    // the installation of the battery?
    g_rtc_has_battery =
        !(rd_now.year == 1970 &&
          rd_now.month == 1 &&
          rd_now.day == 1 &&
          rd_now.hour == 0 &&
          rd_now.min == 0);

    LOGI("rtc_init %s detected a battery",
         (g_rtc_has_battery ? "has" : "has NOT"));

    // Load file and set RTC if found.
    if (file_exists(RTC_FILE)) {
        FILE *fp = fopen(RTC_FILE, "r");
        if (fp) {
            static char buffer[32];

            while (fgets(buffer, sizeof(buffer), fp)) {
                struct rtc_date rd_file;
                sscanf(buffer, RTC_LOG_FORMAT,
                       &rd_file.year,
                       &rd_file.month,
                       &rd_file.day,
                       &rd_file.hour,
                       &rd_file.min,
                       &rd_file.sec);

                // Wishes to reset the clock
                if (rd_file.year == 1970) {
                    LOGI("rtc_init is resetting year back to 1970");
                    rtc_set_clock(&rd_file);
                } else {
                    // Update if file contains a later date
                    if (rd_now.year < rd_file.year ||
                        rd_now.month < rd_file.month ||
                        rd_now.day < rd_file.day ||
                        rd_now.hour < rd_file.hour ||
                        rd_now.min < rd_file.min ||
                        rd_now.sec < rd_file.sec) {
                        LOGI("rtc_init is updating clock to a later date");
                        rtc_set_clock(&rd_file);
                    }
                }
            }
            fclose(fp);
        }
    }

    rtc_timestamp();
}

/**
 *  Return 1 if detected battery otherwise 0.
 */
int rtc_has_battery() {
    return g_rtc_has_battery;
}

/**
 *  Format RTC in a standard format
 */
void rtc_print(const struct rtc_date *rd) {
    LOGI(RTC_LOG_FORMAT,
         rd->year,
         rd->month,
         rd->day,
         rd->hour,
         rd->min,
         rd->sec);
}

/**
 *  Log RTC in a standard format
 */
void rtc_timestamp() {
    struct rtc_date rd;
    rtc_get_clock(&rd);
    rtc_print(&rd);
}

/**
 *  Set Hardware Clock
 */
void rtc_set_clock(const struct rtc_date *rd) {
    int fd = open(RTC_DEV, O_WRONLY);
    if (fd >= 0) {
        struct rtc_time rt;
        rd2rt(rd, &rt);
        if (ioctl(fd, RTC_SET_TIME, &rt) != 0) {
            LOGE("ioctl(%d,RTC_SET_TIME,rt) failed with errno(%d)", fd, errno);
        }
        close(fd);
    } else {
        LOGE("rtc_set_clock failed to open(%s, O_RDWR)", RTC_DEV);
    }
}

/**
 *  Get Hardware Clock
 */
void rtc_get_clock(struct rtc_date *rd) {
    int fd = open(RTC_DEV, O_RDONLY);
    if (fd >= 0) {
        struct rtc_time rt;
        if (ioctl(fd, RTC_RD_TIME, &rt) == 0) {
            rt2rd(&rt, rd);
        } else {
            LOGE("ioctl(%d,RTC_RD_TIME,rt) failed with errno(%d)", fd, errno);
        }
        close(fd);
    } else {
        LOGE("rtc_get_clock failed to open(%s, O_RDWR)", RTC_DEV);
    }
}

/**
 *  Formats buffer with an ISO-8061 Filesystem safe string.
 *  Returns the number of characters written.
 */
int rtc_get_clock_iso_str(char *buffer, int size) {
    struct rtc_date rd;
    rtc_get_clock(&rd);
    return snprintf(buffer, size, RTC_ISO_FORMAT,
                    rd.year,
                    rd.month,
                    rd.day,
                    rd.hour,
                    rd.min,
                    rd.sec);
}

/**
 *  Formats buffer with an OSD pretty string.
 *  Returns the number of characters written.
 */
int rtc_get_clock_osd_str(char *buffer, int size) {
    struct rtc_date rd;
    rtc_get_clock(&rd);
    return snprintf(buffer, size, RTC_OSD_FORMAT,
                    rd.year,
                    rd.month,
                    rd.day,
                    rd.hour,
                    rd.min,
                    rd.sec);
}