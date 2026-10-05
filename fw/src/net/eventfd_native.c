/*
 * native_sim with the host C library (CONFIG_NATIVE_LIBC): Zephyr does not
 * build lib/posix there, so the HTTP server's eventfd() call is linked to
 * the host's eventfd() and gets a host descriptor number. That number is
 * not in the Zephyr descriptor table; once a socket gets the same number,
 * the server polls it twice and NSOS exits ("EPOLL_CTL_ADD: errno=17").
 * These definitions are inside the Zephyr image, so its calls bind here.
 */
#include <zephyr/zvfs/eventfd.h>

int eventfd(unsigned int initval, int flags)
{
	return zvfs_eventfd(initval, flags);
}

int eventfd_read(int fd, zvfs_eventfd_t *value)
{
	return zvfs_eventfd_read(fd, value);
}

int eventfd_write(int fd, zvfs_eventfd_t value)
{
	return zvfs_eventfd_write(fd, value);
}
