#include "tz_rule.h"

/* Avalia o formato TZ do POSIX (inclusive horario de verao).
   O campo de offset do POSIX e positivo a oeste; aqui o resultado e leste. */

typedef struct {
    int kind; /* 1 = Mm.n.d, 2 = Jn, 3 = n */
    int a, b, c;
    int time_sec;
} tz_when;

typedef struct {
    int std_east;
    int dst_east;
    int has_dst;
    tz_when start;
    tz_when end;
} tz_rule;

static int is_leap(int y)
{
    return ((y % 4) == 0 && (y % 100) != 0) || ((y % 400) == 0);
}

static const char *parse_uint(const char *p, int *out)
{
    int v = 0;
    if (!p) {
        *out = 0;
        return p;
    }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = v;
    return p;
}

/* hh[:mm[:ss]] com sinal opcional. O sinal vale so para a hora. */
static const char *parse_hms(const char *p, int *sec_out)
{
    int sign = 1;
    int h = 0, m = 0, s = 0;
    if (!p) {
        *sec_out = 0;
        return p;
    }
    if (*p == '+') {
        p++;
    } else if (*p == '-') {
        sign = -1;
        p++;
    }
    p = parse_uint(p, &h);
    if (*p == ':') {
        p++;
        p = parse_uint(p, &m);
    }
    if (*p == ':') {
        p++;
        p = parse_uint(p, &s);
    }
    *sec_out = sign * (h * 3600 + m * 60 + s);
    return p;
}

static const char *parse_name(const char *p)
{
    if (!p || !*p) return p;
    if (*p == '<') {
        p++;
        while (*p && *p != '>') p++;
        if (*p == '>') p++;
        return p;
    }
    const char *s = p;
    while (*p && *p != ',' && *p != '+' && *p != '-' && !(*p >= '0' && *p <= '9')) p++;
    if ((int)(p - s) < 3) return s;
    return p;
}

static const char *parse_when(const char *p, tz_when *w)
{
    w->kind = 0;
    w->a = w->b = w->c = 0;
    w->time_sec = 2 * 3600;
    if (!p || !*p) return p;
    if (*p == 'M') {
        w->kind = 1;
        p++;
        p = parse_uint(p, &w->a);
        if (*p == '.') p++;
        p = parse_uint(p, &w->b);
        if (*p == '.') p++;
        p = parse_uint(p, &w->c);
    } else if (*p == 'J') {
        w->kind = 2;
        p++;
        p = parse_uint(p, &w->a);
    } else if (*p >= '0' && *p <= '9') {
        w->kind = 3;
        p = parse_uint(p, &w->a);
    }
    if (*p == '/') {
        p++;
        p = parse_hms(p, &w->time_sec);
    }
    return p;
}

static int parse_rule(const char *posix, tz_rule *r)
{
    const char *p = posix;
    int west = 0;
    r->std_east = 0;
    r->dst_east = 0;
    r->has_dst = 0;
    r->start.kind = 0;
    r->end.kind = 0;
    if (!p || !*p) return 0;
    p = parse_name(p);
    if (p == posix) return 0;
    p = parse_hms(p, &west);
    r->std_east = -west;
    if (*p == 0 || *p == ',') return 1;
    const char *name = p;
    p = parse_name(p);
    if (p == name) return 1;
    if (*p == '+' || *p == '-' || (*p >= '0' && *p <= '9')) {
        p = parse_hms(p, &west);
        r->dst_east = -west;
    } else {
        r->dst_east = r->std_east + 3600;
    }
    r->has_dst = 1;
    if (*p == ',') {
        p++;
        p = parse_when(p, &r->start);
        if (*p == ',') {
            p++;
            p = parse_when(p, &r->end);
        }
    }
    return 1;
}

static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y_out, int *m_out, int *d_out)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int y = (int)yoe + (int)era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned m = mp < 10u ? mp + 3u : mp - 9u;
    y += (m <= 2u);
    *y_out = y;
    *m_out = (int)m;
    *d_out = (int)d;
}

static int weekday_sun0(int y, int m, int d)
{
    int64_t days = days_from_civil(y, m, d);
    int w = (int)((days + 4) % 7);
    if (w < 0) w += 7;
    return w;
}

static int yday0_of(int y, int month, int day)
{
    static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int yd = cum[month - 1] + day - 1;
    if (month > 2 && is_leap(y)) yd++;
    return yd;
}

static int dim_of(int y, int month)
{
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap(y)) return 29;
    if (month < 1 || month > 12) return 30;
    return d[month - 1];
}

static int when_yday0(int year, const tz_when *w)
{
    if (w->kind == 1) {
        int month = w->a;
        int week = w->b;
        int wday = w->c;
        if (month < 1) month = 1;
        if (month > 12) month = 12;
        if (week < 1) week = 1;
        if (wday < 0) wday = 0;
        if (wday > 6) wday = 6;
        int first = weekday_sun0(year, month, 1);
        int day = 1 + (wday - first + 7) % 7 + (week - 1) * 7;
        int dim = dim_of(year, month);
        if (day > dim) day -= 7;
        if (day < 1) day = 1;
        return yday0_of(year, month, day);
    }
    if (w->kind == 2) {
        int n = w->a;
        if (n < 1) n = 1;
        if (is_leap(year) && n >= 60) return n;
        return n - 1;
    }
    if (w->kind == 3) return w->a;
    return 0;
}

static int64_t transition_utc(int year, const tz_when *w, int east)
{
    int64_t year_start = days_from_civil(year, 1, 1) * 86400;
    return year_start + (int64_t)when_yday0(year, w) * 86400 + w->time_sec - east;
}

int tz_posix_east(const char *posix, int64_t utc)
{
    tz_rule r;
    if (!parse_rule(posix, &r) || !r.has_dst || r.start.kind == 0 || r.end.kind == 0) {
        return r.std_east;
    }
    int year = 1970, month = 1, day = 1;
    int64_t days = utc / 86400;
    if (utc < 0 && (utc % 86400) != 0) days--;
    civil_from_days(days, &year, &month, &day);
    int64_t start = transition_utc(year, &r.start, r.std_east);
    int64_t end = transition_utc(year, &r.end, r.dst_east);
    int in_dst;
    if (start < end) in_dst = utc >= start && utc < end;
    else in_dst = utc >= start || utc < end;
    return in_dst ? r.dst_east : r.std_east;
}
