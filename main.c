#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "mbedtls/ssl.h"
#include "secrets.h"

/* ── Debug ───────────────────────────────────────────────────────────────── */
// #define DEBUG   /* décommenter pour activer les traces UART */
#ifdef DEBUG
#  define DBG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#  define DBG(fmt, ...) ((void)0)
#endif

/* ── Configuration ────────────────────────────────────────────────────────── */
#define PRIM_HOST        "prim.iledefrance-mobilites.fr"
#define PRIM_PORT        443
#define NAVITIA_URI_V    "/marketplace/v2/navitia/stop_areas/" \
                         "stop_area%3AIDFM%3A63404/departures" \
                         "?count=40&duration=10800"
#define NAVITIA_URI_V_THEO  "/marketplace/v2/navitia/stop_areas/" \
                            "stop_area%3AIDFM%3A63404/departures" \
                            "?count=40&duration=10800&data_freshness=base_schedule"
#define NAVITIA_URI_BUS  "/marketplace/v2/navitia/stop_areas/" \
                         "stop_area%3AIDFM%3A63415/departures" \
                         "?count=40&duration=10800"
#define NAVITIA_URI_BUS_THEO  "/marketplace/v2/navitia/stop_areas/" \
                              "stop_area%3AIDFM%3A63415/departures" \
                              "?count=40&duration=10800&data_freshness=base_schedule"
#define POLL_INTERVAL_MS  300000u  /* intervalle normal entre deux cycles PRIM (5 min) */
/* Horaires de service (heure Paris locale) :
   V           : 05h00 – 00h00
   Bus 4615    : 06h00 – 22h00
   Bus 6133    : 06h00 – 21h00
   Hors service : 00h00 – 04h59 (toutes lignes arrêtées) */
#define HOUR_SERVICE_START  5u   /* première heure où au moins une ligne roule */
#define HOUR_SERVICE_END   24u   /* toutes lignes arrêtées à partir de cette heure (00h) */
#define REQ_TIMEOUT_MS  30000u    /* timeout global d'une requête DNS/TLS/HTTP */

/* ── GPIO LCD (Pico 2W → LCD HD44780 2×40) ───────────────────────────────── */
#define LCD_RS   3
#define LCD_E    2
#define LCD_DB0  4
#define LCD_DB1  5
#define LCD_DB2  6
#define LCD_DB3  7
#define LCD_DB4  8
#define LCD_DB5  9
#define LCD_DB6  10
#define LCD_DB7  11
#define LCD_COLS 40

/* ── Tampons et état ─────────────────────────────────────────────────────── */
#define MAX_V_MASSY     3
#define MAX_BUS_TIMES   2

/* ── Streaming HTTP/JSON parser ──────────────────────────────────────────── */
#define CTX_SIZE         12288u  /* fenêtre glissante pour la recherche de patterns */
#define LOOKBACK_SIZE     8192u  /* portion conservée entre deux scans */
#define HDR_BUF_SIZE       256u  /* tampon d'en-têtes HTTP */
#define DEP_TO_DISP_MAX    600u  /* distance max departure_date_time → display_informations */
#define DISP_BLOCK_MAX     200u  /* taille du bloc display_informations à scanner */

static char   g_ctx[CTX_SIZE + 4];
static int    g_ctx_len    = 0;
static int    g_ctx_scan   = 0;
static char   g_hbuf[HDR_BUF_SIZE];
static int    g_hbuf_len   = 0;
static bool   g_hdr_done   = false;
static bool   g_is_chunked = false;
static char   g_hdr_tail[4]; /* 4 derniers octets pour détecter \r\n\r\n */
static int    g_hdr_total  = 0;

typedef enum { CS_CHUNK_SIZE, CS_CHUNK_DATA, CS_CHUNK_CRLF, CS_DONE } chunk_state_t;
static chunk_state_t g_cs       = CS_CHUNK_SIZE;
static int           g_crem     = 0;
static char          g_cline[16];
static int           g_cline_len = 0;

typedef enum {
    REQ_IDLE,
    REQ_RESOLVING,
    REQ_CONNECTING,
    REQ_RECEIVING,
    REQ_DONE,
    REQ_ERROR
} req_state_t;

static volatile req_state_t     g_req_state = REQ_IDLE;
static struct altcp_pcb        *g_pcb       = NULL;
static struct altcp_tls_config *g_tls_cfg   = NULL;
static uint32_t                 g_req_start_ms = 0;

typedef enum {
    REQERR_NONE,
    REQERR_TIMEOUT,
    REQERR_DNS_NULL,
    REQERR_DNS_CALL,
    REQERR_TLS_NEW,
    REQERR_CONNECT,
    REQERR_TLS_CB,
    REQERR_TLS_CONN,
    REQERR_WRITE
} req_err_t;

static volatile req_err_t g_req_err = REQERR_NONE;

static const char *g_req_path = NAVITIA_URI_V;

typedef enum {
    FEED_V,
    FEED_V_THEO,
    FEED_BUS,
    FEED_BUS_THEO
} feed_stage_t;

static feed_stage_t g_feed_stage = FEED_V;

/* Horaires parsés */
static char g_v_massy[MAX_V_MASSY][6];
static int  g_v_massy_n = 0;
static char g_4615[MAX_BUS_TIMES][6];
static int  g_4615_n = 0;
static char g_6133[MAX_BUS_TIMES][6];
static int  g_6133_n = 0;
static bool    g_has_data     = false;
static uint8_t g_current_hour = 0xFF; /* 0xFF = heure inconnue (avant 1er fetch) */

/* ═══════════════════════════════════════════════════════════════════════════
   LCD driver
   ══════════════════════════════════════════════════════════════════════════ */

static void lcd_pulse_e(void) {
    gpio_put(LCD_E, 1); sleep_us(1);
    gpio_put(LCD_E, 0); sleep_us(50);
}

static void lcd_write(uint8_t rs, uint8_t data) {
    gpio_put(LCD_RS, rs);
    gpio_put(LCD_DB0, (data >> 0) & 1);
    gpio_put(LCD_DB1, (data >> 1) & 1);
    gpio_put(LCD_DB2, (data >> 2) & 1);
    gpio_put(LCD_DB3, (data >> 3) & 1);
    gpio_put(LCD_DB4, (data >> 4) & 1);
    gpio_put(LCD_DB5, (data >> 5) & 1);
    gpio_put(LCD_DB6, (data >> 6) & 1);
    gpio_put(LCD_DB7, (data >> 7) & 1);
    lcd_pulse_e();
}

static inline void lcd_cmd(uint8_t c)  { lcd_write(0, c); }
static inline void lcd_char(uint8_t c) { lcd_write(1, c); }

static void lcd_goto(uint8_t col, uint8_t row) {
    lcd_cmd(0x80 | ((row ? 0x40u : 0x00u) + col));
    sleep_us(50);
}

static void lcd_init(void) {
    const uint pins[] = { LCD_RS, LCD_E,
        LCD_DB0, LCD_DB1, LCD_DB2, LCD_DB3,
        LCD_DB4, LCD_DB5, LCD_DB6, LCD_DB7 };
    for (size_t i = 0; i < sizeof(pins)/sizeof(pins[0]); i++) {
        gpio_init(pins[i]); gpio_set_dir(pins[i], GPIO_OUT); gpio_put(pins[i], 0);
    }
    sleep_ms(50);
    lcd_cmd(0x30); sleep_ms(5);
    lcd_cmd(0x30); sleep_us(150);
    lcd_cmd(0x30); sleep_us(50);
    lcd_cmd(0x38); sleep_us(50);
    lcd_cmd(0x08); sleep_us(50);
    lcd_cmd(0x01); sleep_ms(2);
    lcd_cmd(0x06); sleep_us(50);
    lcd_cmd(0x0C); sleep_us(50);
}

/* Écrit exactement LCD_COLS caractères sur la ligne (complète avec espaces) */
static void lcd_write_line(uint8_t row, const char *s, int len) {
    lcd_goto(0, row);
    for (int i = 0; i < LCD_COLS; i++)
        lcd_char(i < len ? (uint8_t)s[i] : ' ');
}

static void lcd_printf(uint8_t row, const char *fmt, ...) {
    char buf[LCD_COLS + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lcd_write_line(row, buf, (int)strlen(buf));
}

/* ═══════════════════════════════════════════════════════════════════════════
   Affichage Transilien
   ══════════════════════════════════════════════════════════════════════════ */

static void lcd_show_departures(void) {
    const char *v1 = g_v_massy_n > 0 ? g_v_massy[0] : "--:--";
    const char *v2 = g_v_massy_n > 1 ? g_v_massy[1] : "--:--";
    const char *v3 = g_v_massy_n > 2 ? g_v_massy[2] : "--:--";
    lcd_printf(0, "V>MASSY %s %s %s", v1, v2, v3);

    const char *b1 = g_4615_n > 0 ? g_4615[0] : "--:--";
    const char *b2 = g_4615_n > 1 ? g_4615[1] : "--:--";
    const char *c1 = g_6133_n > 0 ? g_6133[0] : "--:--";
    const char *c2 = g_6133_n > 1 ? g_6133[1] : "--:--";
    lcd_printf(1, "4615 %s %s | 6133 %s %s", b1, b2, c1, c2);
}

#ifdef DEBUG
static void uart_dump_times(const char *tag) {
    DBG("TIMES[%s] V=", tag);
    if (g_v_massy_n == 0) {
        DBG("none");
    } else {
        for (int i = 0; i < g_v_massy_n; i++)
            DBG("%s%s", i ? "," : "", g_v_massy[i]);
    }
    DBG(" | 4615=");
    if (g_4615_n == 0) {
        DBG("none");
    } else {
        for (int i = 0; i < g_4615_n; i++)
            DBG("%s%s", i ? "," : "", g_4615[i]);
    }
    DBG(" | 6133=");
    if (g_6133_n == 0) {
        DBG("none");
    } else {
        for (int i = 0; i < g_6133_n; i++)
            DBG("%s%s", i ? "," : "", g_6133[i]);
    }
    DBG("\n");
}
static void uart_dump_final_compact(void) {
    const char *v1 = g_v_massy_n > 0 ? g_v_massy[0] : "--:--";
    const char *v2 = g_v_massy_n > 1 ? g_v_massy[1] : "--:--";
    const char *v3 = g_v_massy_n > 2 ? g_v_massy[2] : "--:--";
    const char *b1 = g_4615_n > 0 ? g_4615[0] : "--:--";
    const char *b2 = g_4615_n > 1 ? g_4615[1] : "--:--";
    const char *c1 = g_6133_n > 0 ? g_6133[0] : "--:--";
    const char *c2 = g_6133_n > 1 ? g_6133[1] : "--:--";
    for (int i = 0; i < 3; i++)
        DBG("FT|%s|%s|%s|%s|%s|%s|%s\n", v1, v2, v3, b1, b2, c1, c2);
}
#else
#  define uart_dump_times(tag)      ((void)0)
#  define uart_dump_final_compact() ((void)0)
#endif

static const char *req_err_str(req_err_t e) {
    switch (e) {
    case REQERR_TIMEOUT:
        switch (g_req_state) {
        case REQ_RESOLVING: return "TO_DNS";
        case REQ_CONNECTING: return "TO_CONN";
        case REQ_RECEIVING: return "TO_HTTP";
        default: return "TIMEOUT";
        }
    case REQERR_DNS_NULL: return "DNS_NULL";
    case REQERR_DNS_CALL: return "DNS_CALL";
    case REQERR_TLS_NEW: return "TLS_NEW";
    case REQERR_CONNECT: return "CONNECT";
    case REQERR_TLS_CB: return "TLS_CB";
    case REQERR_TLS_CONN: return "TLS_CONN";
    case REQERR_WRITE: return "WRITE";
    case REQERR_NONE:
    default: return "NONE";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   Heure locale — parsée depuis l'en-tête HTTP "Date:"
   ══════════════════════════════════════════════════════════════════════════ */

/* strstr borné : cherche needle dans [start, end) */
static const char *bstrstr(const char *start, const char *end, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen || end <= start || (size_t)(end - start) < nlen) return NULL;
    for (const char *p = start; p <= end - nlen; p++) {
        if (memcmp(p, needle, nlen) == 0) return p;
    }
    return NULL;
}

static int http_month_num(const char *s) {
    static const char * const names[12] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    for (int i = 0; i < 12; i++)
        if (memcmp(s, names[i], 3) == 0) return i + 1;
    return 0;
}

/* DST Europe/Paris : CEST (UTC+2) du dernier dimanche de mars au dernier dimanche
   d'octobre. Approximation : transition aux alentours du 25 du mois. */
static bool paris_is_cest(int m, int d) {
    if (m > 3 && m < 10) return true;
    if (m == 3)  return d >= 25;
    if (m == 10) return d <  25;
    return false;
}

/* Parse "Date: Thu, 24 Apr 2026 10:15:30 GMT" → met à jour g_current_hour. */
static void update_hour_from_hdr(void) {
    /* Recherche "ate: " pour capturer "Date:" et "date:" indifféremment */
    const char *p = bstrstr(g_hbuf, g_hbuf + g_hbuf_len, "ate: ");
    if (!p) return;
    p += 5;
    /* Sauter le nom du jour "Thu, " */
    const char *comma = memchr(p, ',', 10);
    if (!comma) return;
    p = comma + 2;
    /* Format attendu : "24 Apr 2026 10:15:30 GMT" */
    if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return;
    int day = (p[0]-'0')*10 + (p[1]-'0');
    if (p[2] != ' ') return;
    int month = http_month_num(p + 3);
    if (!month || p[6] != ' ') return;
    /* Sauter l'année "2026 " (4 chiffres + espace) */
    if (p[11] != ' ') return;
    if (!isdigit((unsigned char)p[12]) || !isdigit((unsigned char)p[13])) return;
    int h_utc = (p[12]-'0')*10 + (p[13]-'0');
    int offset = paris_is_cest(month, day) ? 2 : 1;
    g_current_hour = (uint8_t)((h_utc + offset) % 24);
    DBG("Heure Paris: %02dh (UTC%+d)\n", g_current_hour, offset);
}

/* Retourne true si toutes les lignes sont à l'arrêt (00h00 – 04h59). */
static bool is_night_hours(void) {
    return g_current_hour != 0xFF && g_current_hour < HOUR_SERVICE_START;
}

/* ═══════════════════════════════════════════════════════════════════════════
   Parseur JSON SIRI Lite
   ══════════════════════════════════════════════════════════════════════════ */

static bool ci_prefix(const char *s, const char *prefix, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!s[i] || tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i]))
            return false;
    }
    return true;
}

static bool extract_navitia_hhmm(const char *raw, char out[6]) {
    if (!raw || strlen(raw) < 13 || raw[8] != 'T') return false;
    if (!isdigit((unsigned char)raw[9]) || !isdigit((unsigned char)raw[10]) ||
        !isdigit((unsigned char)raw[11]) || !isdigit((unsigned char)raw[12])) return false;
    out[0] = raw[9];
    out[1] = raw[10];
    out[2] = ':';
    out[3] = raw[11];
    out[4] = raw[12];
    out[5] = '\0';
    return true;
}

static bool add_unique_time(char dst[][6], int *n, int max_n, const char hhmm[6]) {
    for (int i = 0; i < *n; i++) {
        if (memcmp(dst[i], hhmm, 6) == 0) return false;
    }
    if (*n >= max_n) return false;
    memcpy(dst[*n], hhmm, 6);
    (*n)++;
    return true;
}

/* Cherche les patterns dans la fenêtre courante.
   Ordre réel Navitia : stop_date_time.departure_date_time  (ancre)
                        + ~440 octets →  display_informations.direction + label
   final=true : scanne jusqu'au bout ; sinon, laisse 600 octets de marge
   pour être sûr que display_informations est déjà dans le tampon. */
static void scan_ctx(bool final_scan) {
    static const char dep_key[]  = "\"departure_date_time\":\"";
    static const char disp_key[] = "\"display_informations\":{";

    const char *const ctxend = g_ctx + g_ctx_len;
    /* En mode non-final, ne scanner que les entrées dont on a déjà 600 octets d'avance */
    const char *bend = final_scan ? ctxend : (ctxend - (int)DEP_TO_DISP_MAX);
    if (bend <= g_ctx + g_ctx_scan) return;

    const char *p = g_ctx + g_ctx_scan;
    while (p < bend) {
        /* 1. Trouver departure_date_time */
        const char *dep = bstrstr(p, bend, dep_key);
        if (!dep) break;

        const char *raw = dep + sizeof(dep_key) - 1;  /* valeur brute, ex. "20260423T194800" */
        char hhmm[6];
        if (!extract_navitia_hhmm(raw, hhmm)) {
            p = dep + 1;
            continue;
        }

        /* 2. Chercher display_informations dans les DEP_TO_DISP_MAX octets suivants */
        const char *disp_end = dep + DEP_TO_DISP_MAX;
        if (disp_end > ctxend) disp_end = ctxend;
        const char *disp = bstrstr(dep, disp_end, disp_key);
        if (!disp) {
            /* Pas encore dans le tampon — ne pas avancer au-delà de dep */
            if (!final_scan) { p = dep; break; }
            p = dep + 1;
            continue;
        }

        /* 3. Trouver label et direction dans le bloc display_informations */
        const char *block_end = disp + DISP_BLOCK_MAX;
        if (block_end > ctxend) block_end = ctxend;
        const char *lp = bstrstr(disp, block_end, "\"label\":\"");
        const char *dp = bstrstr(disp, block_end, "\"direction\":\"");

        if (lp && dp) {
            lp += 9;   /* pointe sur la valeur du label */
            dp += 13;  /* pointe sur la valeur de direction */
            if (g_feed_stage == FEED_V || g_feed_stage == FEED_V_THEO) {
                if (ci_prefix(lp, "V\"", 2) &&
                    ci_prefix(dp, "Massy - Palaiseau", 17)) {
                    add_unique_time(g_v_massy, &g_v_massy_n, MAX_V_MASSY, hhmm);
                }
            } else {
                if (ci_prefix(lp, "4615\"", 5) &&
                    ci_prefix(dp, "V\\u00e9lizy 2", 12)) {
                    add_unique_time(g_4615, &g_4615_n, MAX_BUS_TIMES, hhmm);
                } else if (ci_prefix(lp, "6133\"", 5) &&
                           ci_prefix(dp, "Gare de Chaville Rive Droite", 28)) {
                    add_unique_time(g_6133, &g_6133_n, MAX_BUS_TIMES, hhmm);
                }
            }
        }

        p = dep + 1;
    }
    g_ctx_scan = (int)(p - g_ctx);
}

/* Alimente la fenêtre glissante avec des octets JSON décodés. */
static void json_feed_bytes(const char *data, int len) {
    int i = 0;
    while (i < len) {
        int space = (int)CTX_SIZE - g_ctx_len;
        if (space == 0) {
            scan_ctx(false);
            int keep = (int)LOOKBACK_SIZE;
            int discard = (int)CTX_SIZE - keep;
            int old_scan = g_ctx_scan;
            memmove(g_ctx, g_ctx + discard, keep);
            g_ctx_len  = keep;
            g_ctx_scan = (old_scan > discard) ? (old_scan - discard) : 0;
            space = (int)CTX_SIZE - keep;
        }
        int copy = len - i;
        if (copy > space) copy = space;
        memcpy(g_ctx + g_ctx_len, data + i, copy);
        g_ctx_len += copy;
        g_ctx[g_ctx_len] = '\0';
        i += copy;
    }
}

/* Décode la réponse HTTP (en-têtes + chunked) et alimente json_feed_bytes. */
static void stream_process(const char *data, int len) {
    int i = 0;
    while (i < len) {
        char c = data[i];
        if (!g_hdr_done) {
            if (g_hbuf_len < (int)HDR_BUF_SIZE - 1)
                g_hbuf[g_hbuf_len++] = c;
            /* Fenêtre glissante sur 4 octets — fonctionne peu importe la longueur */
            if (g_hdr_total < 4) {
                g_hdr_tail[g_hdr_total] = c;
            } else {
                g_hdr_tail[0] = g_hdr_tail[1];
                g_hdr_tail[1] = g_hdr_tail[2];
                g_hdr_tail[2] = g_hdr_tail[3];
                g_hdr_tail[3] = c;
            }
            g_hdr_total++;
            if (g_hdr_total >= 4 &&
                g_hdr_tail[0] == '\r' && g_hdr_tail[1] == '\n' &&
                g_hdr_tail[2] == '\r' && g_hdr_tail[3] == '\n') {
                g_hdr_done = true;
                g_hbuf[g_hbuf_len] = '\0';
                g_is_chunked = (bstrstr(g_hbuf, g_hbuf + g_hbuf_len, "chunked") != NULL);
            }
            i++; continue;
        }
        if (!g_is_chunked) {
            json_feed_bytes(data + i, len - i);
            i = len; continue;
        }
        switch (g_cs) {
        case CS_CHUNK_SIZE:
            i++;
            if (c == '\n') {
                g_cline[g_cline_len] = '\0';
                g_crem = (int)strtol(g_cline, NULL, 16);
                g_cline_len = 0;
                g_cs = (g_crem == 0) ? CS_DONE : CS_CHUNK_DATA;
            } else if (isxdigit((unsigned char)c) && g_cline_len < 14) {
                g_cline[g_cline_len++] = c;
            }
            break;
        case CS_CHUNK_DATA: {
            int avail = len - i;
            int feed  = avail < g_crem ? avail : g_crem;
            json_feed_bytes(data + i, feed);
            g_crem -= feed; i += feed;
            if (g_crem == 0) g_cs = CS_CHUNK_CRLF;
            break;
        }
        case CS_CHUNK_CRLF:
            i++;
            if (c == '\n') g_cs = CS_CHUNK_SIZE;
            break;
        case CS_DONE:
            i = len; break;
        }
    }
}

static void stream_reset(void) {
    g_ctx_len    = 0; g_ctx_scan  = 0;
    g_hbuf_len   = 0; g_hdr_done  = false; g_is_chunked = false;
    g_hdr_total  = 0; memset(g_hdr_tail, 0, 4);
    g_cs         = CS_CHUNK_SIZE; g_crem = 0; g_cline_len = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
   Client HTTPS raw (altcp_tls)
   ══════════════════════════════════════════════════════════════════════════ */

static void tls_cleanup(void) {
    if (g_pcb) {
        altcp_recv(g_pcb, NULL);
        altcp_err(g_pcb, NULL);
        altcp_close(g_pcb);
        g_pcb = NULL;
    }
}

static void tls_err_cb(void *arg, err_t err) {
    (void)arg;
    DBG("TLS erreur: %d\n", (int)err);
    g_pcb       = NULL;
    g_req_err   = REQERR_TLS_CB;
    g_req_state = REQ_ERROR;
}

static err_t tls_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg;
    if (!p) {
        /* Serveur a fermé la connexion → réponse complète */
        altcp_close(pcb);
        g_pcb       = NULL;
        g_req_state = REQ_DONE;
        return ERR_OK;
    }
    if (err != ERR_OK) { pbuf_free(p); return err; }

    static char tmp[2048];
    u16_t total = p->tot_len;
    u16_t off   = 0;
    while (off < total) {
        u16_t chunk = total - off;
        if (chunk > (u16_t)sizeof(tmp)) chunk = (u16_t)sizeof(tmp);
        pbuf_copy_partial(p, tmp, chunk, off);
        stream_process(tmp, (int)chunk);
        off += chunk;
    }

    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t tls_connected_cb(void *arg, struct altcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK) { g_req_err = REQERR_TLS_CONN; g_req_state = REQ_ERROR; return err; }

    static char req[512];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: " PRIM_HOST "\r\n"
        "apikey: %s\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "\r\n",
        g_req_path,
        PRIM_API_KEY);

    err_t e = altcp_write(pcb, req, (u16_t)n, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) { g_req_err = REQERR_WRITE; g_req_state = REQ_ERROR; return ERR_OK; }
    altcp_output(pcb);
    DBG("Requête HTTPS envoyée\n");
    g_req_state = REQ_RECEIVING;
    return ERR_OK;
}

static void start_connect(const ip_addr_t *addr) {
    g_pcb = altcp_tls_new(g_tls_cfg, IPADDR_TYPE_V4);
    if (!g_pcb) { g_req_err = REQERR_TLS_NEW; g_req_state = REQ_ERROR; return; }

    /* SNI obligatoire pour que le serveur présente le bon certificat */
    mbedtls_ssl_set_hostname(
        (mbedtls_ssl_context *)altcp_tls_context(g_pcb),
        PRIM_HOST);

    altcp_recv(g_pcb, tls_recv_cb);
    altcp_err(g_pcb, tls_err_cb);

    err_t e = altcp_connect(g_pcb, addr, PRIM_PORT, tls_connected_cb);
    if (e != ERR_OK) {
        DBG("altcp_connect: %d\n", (int)e);
        altcp_close(g_pcb);
        g_pcb       = NULL;
        g_req_err   = REQERR_CONNECT;
        g_req_state = REQ_ERROR;
        return;
    }
    g_req_state = REQ_CONNECTING;
}

static void dns_found_cb(const char *name, const ip_addr_t *addr, void *arg) {
    (void)name; (void)arg;
    if (!addr) { DBG("DNS échec\n"); g_req_err = REQERR_DNS_NULL; g_req_state = REQ_ERROR; return; }
    DBG("DNS: %s\n", ip4addr_ntoa(addr));
    start_connect(addr);
}

static void start_fetch(const char *path) {
    stream_reset();
    if (g_feed_stage == FEED_V) { g_v_massy_n = 0; }
    else if (g_feed_stage == FEED_BUS) { g_4615_n = 0; g_6133_n = 0; }
    g_req_path  = path;
    g_req_err   = REQERR_NONE;
    g_req_state = REQ_RESOLVING;
    g_req_start_ms = to_ms_since_boot(get_absolute_time());

    static ip_addr_t server_ip;
    err_t e = dns_gethostbyname(PRIM_HOST, &server_ip, dns_found_cb, NULL);
    if (e == ERR_OK) {
        /* Adresse en cache — connexion directe */
        start_connect(&server_ip);
    } else if (e != ERR_INPROGRESS) {
        DBG("dns_gethostbyname: %d\n", (int)e);
        g_req_err = REQERR_DNS_CALL;
        g_req_state = REQ_ERROR;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   main
   ══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    stdio_init_all();

    if (cyw43_arch_init()) { DBG("cyw43_arch_init failed\n"); return 1; }

    lcd_init();
    DBG("LCD init OK\n");

    lcd_write_line(0, "Connexion WiFi...", 17);
    lcd_write_line(1, WIFI_SSID, (int)strlen(WIFI_SSID));

    cyw43_arch_enable_sta_mode();
    DBG("WiFi '%s'...\n", WIFI_SSID);

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
            CYW43_AUTH_WPA2_MIXED_PSK, 30000) != 0) {
        DBG("WiFi ECHEC\n");
        lcd_write_line(0, "WiFi ECHEC", 10);
        lcd_write_line(1, "", 0);
        while (true) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); sleep_ms(100);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); sleep_ms(100);
        }
    }

    for (int i = 0; i < 50; i++) {
        if (!ip4_addr_isany_val(*netif_ip4_addr(netif_list))) break;
        sleep_ms(200);
    }

    char ip_str[16];
    strncpy(ip_str, ip4addr_ntoa(netif_ip4_addr(netif_list)), sizeof(ip_str) - 1);
    ip_str[sizeof(ip_str) - 1] = '\0';
    DBG("WiFi OK — %s\n", ip_str);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    /* Force des DNS publics: certains DNS de box répondent mal au Pico/lwIP */
    ip_addr_t dns_primary;
    ip_addr_t dns_backup;
    ipaddr_aton("8.8.8.8", &dns_primary);
    ipaddr_aton("1.1.1.1", &dns_backup);
    dns_setserver(0, &dns_primary);
    dns_setserver(1, &dns_backup);

    /* Config TLS client : pas de vérification de certificat (pas de CA store embarqué).
       NULL/0 = skip cert verification, acceptable pour un device IoT sans cert store. */
    g_tls_cfg = altcp_tls_create_config_client(NULL, 0);

    lcd_write_line(0, "Chargement...", 13);
    lcd_printf(1, "IP:%s", ip_str);

    /* Première requête immédiate */
    uint32_t last_poll = to_ms_since_boot(get_absolute_time()) - POLL_INTERVAL_MS;

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        /* Traiter les paquets réseau en arrière-plan */
        cyw43_arch_lwip_begin();
        cyw43_arch_poll();
        cyw43_arch_lwip_end();

        /* Timeout de sécurité: évite de rester bloqué indéfiniment sur "Chargement..." */
        if ((g_req_state == REQ_RESOLVING || g_req_state == REQ_CONNECTING || g_req_state == REQ_RECEIVING) &&
            (now - g_req_start_ms) >= REQ_TIMEOUT_MS) {
            g_req_err = REQERR_TIMEOUT;
            g_req_state = REQ_ERROR;
        }

        /* Déclencher le polling périodique */
        if (g_req_state == REQ_IDLE && (now - last_poll) >= POLL_INTERVAL_MS) {
            last_poll = now;
            if (is_night_hours()) {
                /* Toutes les lignes sont à l'arrêt — pas de requête */
                DBG("Pause nuit (h=%02d) — skip\n", g_current_hour);
                if (!g_has_data) {
                    lcd_write_line(0, "Hors service (nuit)", 19);
                    lcd_write_line(1, "", 0);
                }
            } else {
                g_feed_stage = FEED_V;
                DBG("=== Requete Navitia V ===\n");
                cyw43_arch_lwip_begin();
                start_fetch(NAVITIA_URI_V);
                cyw43_arch_lwip_end();
            }
        }

        /* Réponse reçue */
        if (g_req_state == REQ_DONE) {
            g_req_state = REQ_IDLE;
            cyw43_arch_lwip_begin();
            tls_cleanup();
            cyw43_arch_lwip_end();

            update_hour_from_hdr();
            scan_ctx(true);
            g_has_data = true;
            DBG("V Massy: %d | 4615: %d | 6133: %d\n", g_v_massy_n, g_4615_n, g_6133_n);
            if (g_feed_stage == FEED_V) {
                uart_dump_times("V_RT");
            } else if (g_feed_stage == FEED_V_THEO) {
                uart_dump_times("V_RT_PLUS_THEO");
            } else if (g_feed_stage == FEED_BUS) {
                uart_dump_times("BUS_RT");
            } else {
                uart_dump_times("BUS_RT_PLUS_THEO");
            }
            if (g_feed_stage == FEED_V) {
                if (g_v_massy_n < MAX_V_MASSY) {
                    g_feed_stage = FEED_V_THEO;
                    DBG("=== Requete Navitia V THEO (complement V) ===\n");
                    cyw43_arch_lwip_begin();
                    start_fetch(NAVITIA_URI_V_THEO);
                    cyw43_arch_lwip_end();
                } else {
                    g_feed_stage = FEED_BUS;
                    DBG("=== Requete Navitia BUS ===\n");
                    cyw43_arch_lwip_begin();
                    start_fetch(NAVITIA_URI_BUS);
                    cyw43_arch_lwip_end();
                }
            } else if (g_feed_stage == FEED_V_THEO) {
                g_feed_stage = FEED_BUS;
                DBG("=== Requete Navitia BUS ===\n");
                cyw43_arch_lwip_begin();
                start_fetch(NAVITIA_URI_BUS);
                cyw43_arch_lwip_end();
            } else if (g_feed_stage == FEED_BUS &&
                       (g_4615_n < MAX_BUS_TIMES || g_6133_n < MAX_BUS_TIMES)) {
                g_feed_stage = FEED_BUS_THEO;
                DBG("=== Requete Navitia BUS THEO (complement bus) ===\n");
                cyw43_arch_lwip_begin();
                start_fetch(NAVITIA_URI_BUS_THEO);
                cyw43_arch_lwip_end();
            } else {
                uart_dump_times("FINAL_LCD");
                uart_dump_final_compact();
                lcd_show_departures();
            }
        }

        /* Erreur — réessayer après POLL_INTERVAL_MS */
        if (g_req_state == REQ_ERROR) {
            DBG("Erreur requête — retry dans %ds\n", POLL_INTERVAL_MS / 1000);
            update_hour_from_hdr(); /* peut être partiel mais tente quand même */
            g_req_state = REQ_IDLE;
            cyw43_arch_lwip_begin();
            tls_cleanup();
            cyw43_arch_lwip_end();
            last_poll = to_ms_since_boot(get_absolute_time());
            if (!g_has_data) {
                lcd_write_line(0, "Erreur API PRIM", 15);
                lcd_printf(1, "Retry %ds E:%s", POLL_INTERVAL_MS / 1000, req_err_str(g_req_err));
            }
        }

        sleep_ms(100);
    }

    cyw43_arch_deinit();
    return 0;
}
