#include "tsnet_common_inter.h"

char tsnet_last_error[BUFSIZ] = {0};

void tsnet_set_last_error(const char *file, int line, const char *func, char *fmt, ...)
{
  va_list ap;
  char msg[BUFSIZ - 256] = {0};

  memset(tsnet_last_error, 0x00, sizeof(tsnet_last_error));

  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  snprintf(tsnet_last_error, sizeof(tsnet_last_error), "%s(%d line, %s): %s", file, line, func, msg);
}
