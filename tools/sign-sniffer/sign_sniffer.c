/*
 * Bench sniffer for the Mobitec sign line (fw/docs/bench-virtual-sign.md).
 *
 * Reads the RS-485 line through a USB-RS485 adapter (or a raw byte file),
 * splits it into frames and decodes them with the firmware's own code from
 * fw/lib/sign, so the bench sees exactly what the sign would show.
 *
 * Output: one JSON object per line on stdout.
 *   {"type":"start", ...}
 *   {"type":"frame","n":1,"t":12.345,"dur_ms":687,"gap_ms":29313,"len":330,
 *    "addr":6,"ok":true,"code":0,"same":false,"diff_dots":120,"diff_cols":24,
 *    "garbage":0,"line_errors":0,"hex":"ff06a2...","rows":["#.#.", ...]}
 *   {"type":"stats", ...}   at the end (EOF, SIGINT, SIGTERM)
 *
 * Usage:
 *   sign-sniffer --port /dev/ttyUSB0 [--baud 4800]
 *   sign-sniffer --file capture.bin [--baud 4800]   (time from byte count)
 *   sign-sniffer --encode < rows.txt                (11 rows of 102 '#'/'.')
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/serial.h>
#endif

#include <ws/sign.h>

static volatile sig_atomic_t stop;

struct sniff {
  struct ws_mobitec_rx rx;
  double bit_s;         /* seconds per bit */
  double t0;            /* start time */
  double frame_start;   /* time of the first byte of the frame being received */
  double prev_end;      /* time of the last byte of the previous frame */
  bool have_prev;       /* previous frame decoded */
  struct ws_frame prev; /* previous decoded frame */
  uint8_t prev_raw[sizeof(((struct ws_mobitec_rx *)0)->buf)];
  size_t prev_len;
  uint32_t garbage_at_prev; /* rx.garbage when the previous frame ended */
  uint32_t line_errors;     /* framing/parity errors and breaks */
  uint32_t line_errors_at_prev;
  uint64_t bytes;
  uint32_t frames, ok, bad;
};

static void on_signal(int sig) {
  (void)sig;
  stop = 1;
}

static double now_s(void) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static speed_t baud_const(long baud) {
  switch (baud) {
  case 1200:
    return B1200;
  case 2400:
    return B2400;
  case 4800:
    return B4800;
  case 9600:
    return B9600;
  case 19200:
    return B19200;
  case 38400:
    return B38400;
  case 57600:
    return B57600;
  case 115200:
    return B115200;
  default:
    return 0;
  }
}

/* 8N1, raw. Line errors (framing, parity, overrun, break) are not marked in
 * the data: they are read from the driver's counters (TIOCGICOUNT) where the
 * USB-serial driver keeps them (ftdi_sio does); otherwise the "port" line
 * says line_errors_supported:false. */
static int open_port(const char *path, long baud) {
  struct termios tio;
  speed_t sp = baud_const(baud);
  int fd;

  if (!sp) {
    fprintf(stderr, "sign-sniffer: unsupported baud %ld\n", baud);
    return -1;
  }
  fd = open(path, O_RDONLY | O_NOCTTY);
  if (fd < 0) {
    fprintf(stderr, "sign-sniffer: %s: %s\n", path, strerror(errno));
    return -1;
  }
  if (tcgetattr(fd, &tio) != 0) {
    fprintf(stderr, "sign-sniffer: %s: not a serial port: %s\n", path,
            strerror(errno));
    close(fd);
    return -1;
  }
  cfmakeraw(&tio);
  tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
  tio.c_cflag |= CS8 | CLOCAL | CREAD;
  tio.c_iflag &= ~(PARMRK | INPCK | ISTRIP | IXON | IXOFF);
  tio.c_cc[VMIN] = 1;
  tio.c_cc[VTIME] = 0;
  cfsetispeed(&tio, sp);
  cfsetospeed(&tio, sp);
  if (tcsetattr(fd, TCSANOW, &tio) != 0) {
    fprintf(stderr, "sign-sniffer: %s: %s\n", path, strerror(errno));
    close(fd);
    return -1;
  }
  tcflush(fd, TCIFLUSH);
  return fd;
}

static void print_hex(const uint8_t *b, size_t n) {
  static const char hx[] = "0123456789abcdef";

  for (size_t i = 0; i < n; i++) {
    putchar(hx[b[i] >> 4]);
    putchar(hx[b[i] & 15]);
  }
}

static void print_rows(const struct ws_frame *f) {
  fputs("[", stdout);
  for (int y = 0; y < WS_H; y++) {
    putchar('"');
    for (int x = 0; x < WS_W; x++) {
      putchar(ws_frame_get(f, x, y) ? '#' : '.');
    }
    putchar('"');
    if (y + 1 < WS_H) {
      putchar(',');
    }
  }
  fputs("]", stdout);
}

static void frame_done(struct sniff *s, double t_end) {
  struct ws_frame f;
  uint8_t addr = 0;
  const uint8_t *raw = s->rx.buf;
  size_t len = s->rx.len;
  int code = ws_mobitec_decode(raw, len, &f, &addr);
  bool same = s->prev_len == len && memcmp(s->prev_raw, raw, len) == 0;
  struct ws_frame_diff d = {0, 0};

  s->frames++;
  if (code == 0) {
    s->ok++;
    if (s->have_prev) {
      d = ws_frame_diff(&s->prev, &f);
    }
  } else {
    s->bad++;
  }

  printf("{\"type\":\"frame\",\"n\":%u,\"t\":%.3f,\"dur_ms\":%.0f,", s->frames,
         s->frame_start - s->t0, (t_end - s->frame_start) * 1000.0);
  if (s->prev_len) {
    printf("\"gap_ms\":%.0f,", (s->frame_start - s->prev_end) * 1000.0);
  } else {
    printf("\"gap_ms\":null,");
  }
  printf("\"len\":%zu,\"addr\":%u,\"ok\":%s,\"code\":%d,\"same\":%s,", len,
         addr, code == 0 ? "true" : "false", code, same ? "true" : "false");
  if (code == 0 && s->have_prev) {
    printf("\"diff_dots\":%d,\"diff_cols\":%d,", d.dots, d.cols);
  } else {
    printf("\"diff_dots\":null,\"diff_cols\":null,");
  }
  printf("\"garbage\":%u,\"line_errors\":%u,\"hex\":\"",
         s->rx.garbage - s->garbage_at_prev,
         s->line_errors - s->line_errors_at_prev);
  print_hex(raw, len);
  fputs("\",\"rows\":", stdout);
  if (code == 0) {
    print_rows(&f);
  } else {
    fputs("null", stdout);
  }
  fputs("}\n", stdout);
  fflush(stdout);

  if (code == 0) {
    s->prev = f;
    s->have_prev = true;
  }
  memcpy(s->prev_raw, raw, len);
  s->prev_len = len;
  s->prev_end = t_end;
  s->garbage_at_prev = s->rx.garbage;
  s->line_errors_at_prev = s->line_errors;
}

static void feed(struct sniff *s, uint8_t b, double t) {
  bool idle = s->rx.n == 0;
  enum ws_mobitec_rx_result r = ws_mobitec_rx_feed(&s->rx, b);

  s->bytes++;
  if (idle && s->rx.n == 1) {
    s->frame_start = t;
  }
  if (r == WS_RX_FRAME) {
    frame_done(s, t);
  } else if (r == WS_RX_OVERFLOW) {
    printf("{\"type\":\"overflow\",\"t\":%.3f}\n", t - s->t0);
    fflush(stdout);
  }
}

static void print_stats(const struct sniff *s) {
  printf(
      "{\"type\":\"stats\",\"bytes\":%llu,\"frames\":%u,\"ok\":%u,\"bad\":%u,"
      "\"garbage\":%u,\"overflows\":%u,\"line_errors\":%u}\n",
      (unsigned long long)s->bytes, s->frames, s->ok, s->bad, s->rx.garbage,
      s->rx.overflows, s->line_errors);
  fflush(stdout);
}

/* Sum of the driver's line error counters, or -1 if not available. */
static long long line_error_count(int fd) {
#if defined(__linux__) && defined(TIOCGICOUNT)
  struct serial_icounter_struct ic;

  if (ioctl(fd, TIOCGICOUNT, &ic) == 0) {
    return (long long)ic.frame + ic.parity + ic.overrun + ic.brk +
           ic.buf_overrun;
  }
#endif
  (void)fd;
  return -1;
}

/* Port: a read returns a burst of bytes; the time of byte i is estimated
 * back from the time the read returned (byte time = 10 bits). */
static int run_port(struct sniff *s, int fd) {
  uint8_t buf[512];
  long long base = line_error_count(fd);

  while (!stop) {
    ssize_t n = read(fd, buf, sizeof(buf));
    double t = now_s();

    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      fprintf(stderr, "sign-sniffer: read: %s\n", strerror(errno));
      return 1;
    }
    if (n == 0) {
      fprintf(stderr, "sign-sniffer: port closed\n");
      return 1;
    }
    if (base >= 0) {
      long long cnt = line_error_count(fd);

      if (cnt >= base) {
        s->line_errors = (uint32_t)(cnt - base);
      }
    }
    for (ssize_t i = 0; i < n; i++) {
      feed(s, buf[i], t - (double)(n - 1 - i) * 10.0 * s->bit_s);
    }
  }
  return 0;
}

/* File: time is the byte position at the given baud rate, so a capture is
 * replayed with its line timing (no gaps between frames). */
static int run_file(struct sniff *s, FILE *f) {
  int c;
  uint64_t i = 0;

  while (!stop && (c = fgetc(f)) != EOF) {
    feed(s, (uint8_t)c, s->t0 + (double)i * 10.0 * s->bit_s);
    i++;
  }
  return 0;
}

/* --encode: 11 lines of 102 '#'/'.' (an optional '|' at both ends, as in
 * "ws sign frame") to the hex of the Mobitec frame. */
static int run_encode(void) {
  struct ws_frame f;
  char line[256];
  int y = 0;
  uint8_t out[WS_MOBITEC_MAX];
  size_t n;

  ws_frame_clear(&f);
  while (y < WS_H && fgets(line, sizeof(line), stdin)) {
    char *p = line;
    int x = 0;

    while (*p == ' ' || *p == '|') {
      p++;
    }
    if (*p == '\n' || *p == '\0') {
      continue;
    }
    for (; x < WS_W && (p[x] == '#' || p[x] == '.'); x++) {
      ws_frame_set(&f, x, y, p[x] == '#');
    }
    if (x != WS_W) {
      fprintf(stderr, "sign-sniffer: row %d has %d dots, need %d\n", y, x,
              WS_W);
      return 2;
    }
    y++;
  }
  if (y != WS_H) {
    fprintf(stderr, "sign-sniffer: %d rows, need %d\n", y, WS_H);
    return 2;
  }
  n = ws_mobitec_encode(&f, WS_MOBITEC_ADDR, out, sizeof(out));
  print_hex(out, n);
  putchar('\n');
  return 0;
}

static void usage(void) {
  fputs("usage: sign-sniffer --port DEV [--baud 4800]\n"
        "       sign-sniffer --file FILE|- [--baud 4800]\n"
        "       sign-sniffer --encode < rows.txt\n",
        stderr);
}

int main(int argc, char **argv) {
  static const struct option opts[] = {
      {"port", required_argument, NULL, 'p'},
      {"file", required_argument, NULL, 'f'},
      {"baud", required_argument, NULL, 'b'},
      {"encode", no_argument, NULL, 'e'},
      {"help", no_argument, NULL, 'h'},
      {NULL, 0, NULL, 0},
  };
  const char *port = NULL, *file = NULL;
  long baud = 4800;
  bool encode = false;
  int c, rc;
  static struct sniff s;

  while ((c = getopt_long(argc, argv, "p:f:b:eh", opts, NULL)) != -1) {
    switch (c) {
    case 'p':
      port = optarg;
      break;
    case 'f':
      file = optarg;
      break;
    case 'b':
      baud = strtol(optarg, NULL, 10);
      break;
    case 'e':
      encode = true;
      break;
    default:
      usage();
      return c == 'h' ? 0 : 2;
    }
  }
  if (encode) {
    return run_encode();
  }
  if (!port == !file || baud <= 0) {
    usage();
    return 2;
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  ws_mobitec_rx_init(&s.rx);
  s.bit_s = 1.0 / (double)baud;
  s.t0 = now_s();

  if (port) {
    int fd = open_port(port, baud);

    if (fd < 0) {
      return 1;
    }
    printf("{\"type\":\"start\",\"source\":\"%s\",\"baud\":%ld}\n", port, baud);
    printf("{\"type\":\"port\",\"line_errors_supported\":%s}\n",
           line_error_count(fd) >= 0 ? "true" : "false");
    fflush(stdout);
    rc = run_port(&s, fd);
    close(fd);
  } else {
    FILE *f = strcmp(file, "-") == 0 ? stdin : fopen(file, "rb");

    if (!f) {
      fprintf(stderr, "sign-sniffer: %s: %s\n", file, strerror(errno));
      return 1;
    }
    printf("{\"type\":\"start\",\"source\":\"%s\",\"baud\":%ld}\n", file, baud);
    fflush(stdout);
    rc = run_file(&s, f);
    if (f != stdin) {
      fclose(f);
    }
  }
  print_stats(&s);
  return rc;
}
