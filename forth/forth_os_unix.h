#include <errno.h>
#include <unistd.h>

static void prim_os_error_message(void) { push_c_string(strerror(popint())); }

static void prim_os_exit(void) __attribute__((__noreturn__));

static void prim_os_exit(void) { exit(popint()); }

static bool io_loop(ssize_t n)
{
    if ((flag = (n >= 0))) {
        push((uintptr_t)n);
        return false;
    }
    if (errno != EINTR) {
        pushsigned(errno);
        return false;
    }
    return true;
}

static void prim_os_read(void)
{
    int fd = popint();
    size_t nbyte = popsize();
    void *bytes = poppointer();
    while (io_loop(read(fd, bytes, nbyte)))
        ;
}

static void prim_os_write(void)
{
    int fd = popint();
    size_t nbyte = popsize();
    void *bytes = poppointer();
    while (io_loop(write(fd, bytes, nbyte)))
        ;
}
