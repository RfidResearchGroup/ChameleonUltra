/*
Flag --specs=nano.specs forces linking against libc_nano which contains .gnu.warnings since GCC 11.3 like:
  arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi/arm-none-eabi/lib/thumb/v7e-m+fp/hard/libc_nano.a(libc_a-closer.o): in function `_close_r':
  closer.c:(.text._close_r+0xc): warning: _close is not implemented and will always fail
So we are providing stubs to suppress these warnings
See also https://stackoverflow.com/questions/73742774/gcc-arm-none-eabi-11-3-is-not-implemented-and-will-always-fail
*/

#include <sys/stat.h>
#include <errno.h>
#undef errno
extern int errno;

int _close(int file) {
  errno = EINVAL;
  return -1;
}

int _fstat(int file, struct stat *st) {
  errno = EINVAL;
  return -1;
}

int _getpid(void) {
  return 1;
}

int _isatty(int file) {
  errno = EINVAL;
  return 0;
}

int _kill(int pid, int sig) {
  errno = EINVAL;
  return -1;
}

int _lseek(int file, int ptr, int dir) {
  errno = EINVAL;
  return -1;
}

int _read(int file, char *ptr, int len) {
  errno = EINVAL;
  return -1;
}

int _write(int file, char *ptr, int len) {
  errno = EINVAL;
  return -1;
}
