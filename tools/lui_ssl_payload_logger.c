#define _GNU_SOURCE

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef int (*ssl_rw_fn)(SSL *, void *, int);
typedef int (*ssl_r_fn)(SSL *, void *, int);
typedef int (*ssl_write_ex_fn)(SSL *, const void *, size_t, size_t *);
typedef int (*ssl_read_ex_fn)(SSL *, void *, size_t, size_t *);
typedef void (*ssl_set_verify_fn)(SSL *, int, SSL_verify_cb);
typedef void (*ssl_ctx_set_verify_fn)(SSL_CTX *, int, SSL_verify_cb);
typedef long (*ssl_get_verify_result_fn)(const SSL *);
typedef int (*x509_verify_cert_fn)(X509_STORE_CTX *);
typedef int (*bio_rw_fn)(BIO *, const void *, int);
typedef int (*bio_r_fn)(BIO *, void *, int);

static ssl_rw_fn real_ssl_write = NULL;
static ssl_r_fn real_ssl_read = NULL;
static ssl_write_ex_fn real_ssl_write_ex = NULL;
static ssl_read_ex_fn real_ssl_read_ex = NULL;
static ssl_set_verify_fn real_ssl_set_verify = NULL;
static ssl_ctx_set_verify_fn real_ssl_ctx_set_verify = NULL;
static ssl_get_verify_result_fn real_ssl_get_verify_result = NULL;
static x509_verify_cert_fn real_x509_verify_cert = NULL;
static bio_rw_fn real_bio_write = NULL;
static bio_r_fn real_bio_read = NULL;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_index_fd = -1;
static int g_payload_fd = -1;
static int g_enabled = 0;
static int g_skip_verify = 0;
static unsigned long long g_sequence = 0;

static const char *DIR_OUT = "out";
static const char *DIR_IN = "in";

struct payload_header {
  uint32_t magic;
  uint16_t version;
  uint16_t header_size;
  uint64_t sequence;
  int64_t sec;
  int64_t nsec;
  uint32_t pid;
  uint32_t tid;
  uint32_t direction;
  uint32_t reserved;
  uint64_t payload_len;
};

static long current_tid(void) {
  return syscall(SYS_gettid);
}

static void safe_write_all(int fd, const void *buf, size_t len) {
  const unsigned char *ptr = (const unsigned char *)buf;
  while (len > 0) {
    ssize_t written = write(fd, ptr, len);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    ptr += (size_t)written;
    len -= (size_t)written;
  }
}

static void ensure_parents(const char *path) {
  char tmp[PATH_MAX];
  size_t len = strlen(path);
  if (len >= sizeof(tmp)) {
    return;
  }
  memcpy(tmp, path, len + 1);
  for (size_t i = 1; i < len; ++i) {
    if (tmp[i] == '/') {
      tmp[i] = '\0';
      mkdir(tmp, 0755);
      tmp[i] = '/';
    }
  }
}

static void open_logs(void) {
  const char *index_path = getenv("BOOSTER_SSL_LOG_INDEX");
  const char *payload_path = getenv("BOOSTER_SSL_LOG_PAYLOAD");
  if (!index_path || !*index_path) {
    index_path = getenv("BOOSTER_LUI_SSL_LOG_INDEX");
  }
  if (!payload_path || !*payload_path) {
    payload_path = getenv("BOOSTER_LUI_SSL_LOG_PAYLOAD");
  }
  if (!index_path || !*index_path || !payload_path || !*payload_path) {
    g_enabled = 0;
    return;
  }

  ensure_parents(index_path);
  ensure_parents(payload_path);

  g_index_fd = open(index_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
  g_payload_fd = open(payload_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (g_index_fd < 0 || g_payload_fd < 0) {
    if (g_index_fd >= 0) {
      close(g_index_fd);
      g_index_fd = -1;
    }
    if (g_payload_fd >= 0) {
      close(g_payload_fd);
      g_payload_fd = -1;
    }
    g_enabled = 0;
    return;
  }

  g_enabled = 1;
}

static void load_flags(void) {
  const char *skip_verify = getenv("BOOSTER_SSL_SKIP_VERIFY");
  if (!skip_verify || !*skip_verify) {
    skip_verify = getenv("BOOSTER_LUI_SSL_SKIP_VERIFY");
  }
  g_skip_verify = skip_verify && skip_verify[0] && strcmp(skip_verify, "0") != 0;
}

static const char *peer_string_from_ssl(SSL *ssl, char *buf, size_t buf_size) {
  if (!buf || buf_size == 0) {
    return "";
  }
  buf[0] = '\0';
  if (!ssl) {
    return "";
  }

  int fd = SSL_get_fd(ssl);
  if (fd < 0) {
    return "";
  }

  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) != 0) {
    return "";
  }

  void *src = NULL;
  uint16_t port = 0;
  if (addr.ss_family == AF_INET) {
    struct sockaddr_in *in = (struct sockaddr_in *)&addr;
    src = &in->sin_addr;
    port = ntohs(in->sin_port);
  } else if (addr.ss_family == AF_INET6) {
    struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&addr;
    src = &in6->sin6_addr;
    port = ntohs(in6->sin6_port);
  } else {
    return "";
  }

  char ip[INET6_ADDRSTRLEN];
  if (!inet_ntop(addr.ss_family, src, ip, sizeof(ip))) {
    return "";
  }

  snprintf(buf, buf_size, "%s:%u", ip, (unsigned int)port);
  return buf;
}

static void record_payload(SSL *ssl, const void *buf, size_t len, uint32_t direction) {
  if (!g_enabled || !buf || len == 0) {
    return;
  }

  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
  }

  char peer[128];
  peer_string_from_ssl(ssl, peer, sizeof(peer));

  pthread_mutex_lock(&g_lock);
  struct payload_header header = {
    .magic = 0x42534c50U,
    .version = 1,
    .header_size = sizeof(struct payload_header),
    .sequence = ++g_sequence,
    .sec = (int64_t)ts.tv_sec,
    .nsec = (int64_t)ts.tv_nsec,
    .pid = (uint32_t)getpid(),
    .tid = (uint32_t)current_tid(),
    .direction = direction,
    .reserved = 0,
    .payload_len = (uint64_t)len,
  };
  off_t offset = lseek(g_payload_fd, 0, SEEK_END);
  if (offset >= 0) {
    safe_write_all(g_payload_fd, &header, sizeof(header));
    safe_write_all(g_payload_fd, buf, len);
    dprintf(g_index_fd,
            "%llu\t%lld.%09lld\t%s\t%u\t%u\t%lld\t%zu\t%s\n",
            (unsigned long long)header.sequence,
            (long long)header.sec,
            (long long)header.nsec,
            direction == 0 ? DIR_OUT : DIR_IN,
            header.pid,
            header.tid,
            (long long)offset,
            len,
            peer[0] ? peer : "-");
  }
  pthread_mutex_unlock(&g_lock);
}

static void init_real_symbols(void) {
  if (!real_ssl_write) {
    real_ssl_write = (ssl_rw_fn)dlsym(RTLD_NEXT, "SSL_write");
  }
  if (!real_ssl_read) {
    real_ssl_read = (ssl_r_fn)dlsym(RTLD_NEXT, "SSL_read");
  }
  if (!real_ssl_write_ex) {
    real_ssl_write_ex = (ssl_write_ex_fn)dlsym(RTLD_NEXT, "SSL_write_ex");
  }
  if (!real_ssl_read_ex) {
    real_ssl_read_ex = (ssl_read_ex_fn)dlsym(RTLD_NEXT, "SSL_read_ex");
  }
  if (!real_ssl_set_verify) {
    real_ssl_set_verify = (ssl_set_verify_fn)dlsym(RTLD_NEXT, "SSL_set_verify");
  }
  if (!real_ssl_ctx_set_verify) {
    real_ssl_ctx_set_verify = (ssl_ctx_set_verify_fn)dlsym(RTLD_NEXT, "SSL_CTX_set_verify");
  }
  if (!real_ssl_get_verify_result) {
    real_ssl_get_verify_result = (ssl_get_verify_result_fn)dlsym(RTLD_NEXT, "SSL_get_verify_result");
  }
  if (!real_x509_verify_cert) {
    real_x509_verify_cert = (x509_verify_cert_fn)dlsym(RTLD_NEXT, "X509_verify_cert");
  }
  if (!real_bio_write) {
    real_bio_write = (bio_rw_fn)dlsym(RTLD_NEXT, "BIO_write");
  }
  if (!real_bio_read) {
    real_bio_read = (bio_r_fn)dlsym(RTLD_NEXT, "BIO_read");
  }
}

__attribute__((constructor))
static void payload_logger_init(void) {
  init_real_symbols();
  load_flags();
  open_logs();
}

__attribute__((destructor))
static void payload_logger_fini(void) {
  if (g_index_fd >= 0) {
    close(g_index_fd);
    g_index_fd = -1;
  }
  if (g_payload_fd >= 0) {
    close(g_payload_fd);
    g_payload_fd = -1;
  }
}

int SSL_write(SSL *ssl, const void *buf, int num) {
  init_real_symbols();
  if (!real_ssl_write) {
    errno = ENOSYS;
    return -1;
  }
  if (buf && num > 0) {
    record_payload(ssl, buf, (size_t)num, 0);
  }
  return real_ssl_write(ssl, (void *)buf, num);
}

int SSL_read(SSL *ssl, void *buf, int num) {
  init_real_symbols();
  if (!real_ssl_read) {
    errno = ENOSYS;
    return -1;
  }
  int rc = real_ssl_read(ssl, buf, num);
  if (rc > 0 && buf) {
    record_payload(ssl, buf, (size_t)rc, 1);
  }
  return rc;
}

int SSL_write_ex(SSL *ssl, const void *buf, size_t num, size_t *written) {
  init_real_symbols();
  if (!real_ssl_write_ex) {
    errno = ENOSYS;
    return 0;
  }
  int result = real_ssl_write_ex(ssl, buf, num, written);
  if (result == 1 && buf && written && *written > 0) {
    record_payload(ssl, buf, *written, 0);
  }
  return result;
}

int SSL_read_ex(SSL *ssl, void *buf, size_t num, size_t *readbytes) {
  init_real_symbols();
  if (!real_ssl_read_ex) {
    errno = ENOSYS;
    return 0;
  }
  int result = real_ssl_read_ex(ssl, buf, num, readbytes);
  if (result == 1 && buf && readbytes && *readbytes > 0) {
    record_payload(ssl, buf, *readbytes, 1);
  }
  return result;
}

void SSL_set_verify(SSL *ssl, int mode, SSL_verify_cb callback) {
  init_real_symbols();
  if (!real_ssl_set_verify) {
    return;
  }
  if (g_skip_verify) {
    real_ssl_set_verify(ssl, SSL_VERIFY_NONE, NULL);
    return;
  }
  real_ssl_set_verify(ssl, mode, callback);
}

void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, SSL_verify_cb callback) {
  init_real_symbols();
  if (!real_ssl_ctx_set_verify) {
    return;
  }
  if (g_skip_verify) {
    real_ssl_ctx_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    return;
  }
  real_ssl_ctx_set_verify(ctx, mode, callback);
}

long SSL_get_verify_result(const SSL *ssl) {
  init_real_symbols();
  if (g_skip_verify) {
    return X509_V_OK;
  }
  if (!real_ssl_get_verify_result) {
    return X509_V_OK;
  }
  return real_ssl_get_verify_result(ssl);
}

int X509_verify_cert(X509_STORE_CTX *ctx) {
  init_real_symbols();
  if (g_skip_verify) {
    return 1;
  }
  if (!real_x509_verify_cert) {
    return 1;
  }
  return real_x509_verify_cert(ctx);
}

int BIO_write(BIO *bio, const void *buf, int num) {
  init_real_symbols();
  if (!real_bio_write) {
    errno = ENOSYS;
    return -1;
  }
  int result = real_bio_write(bio, buf, num);
  if (result > 0 && bio && buf) {
    record_payload(NULL, buf, (size_t)result, 0);
  }
  return result;
}

int BIO_read(BIO *bio, void *buf, int num) {
  init_real_symbols();
  if (!real_bio_read) {
    errno = ENOSYS;
    return -1;
  }
  int result = real_bio_read(bio, buf, num);
  if (result > 0 && bio && buf) {
    record_payload(NULL, buf, (size_t)result, 1);
  }
  return result;
}
