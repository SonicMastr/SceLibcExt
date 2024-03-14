#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include <psp2/net/net.h>
#include <psp2/kernel/processmgr.h>

#include <sys/param.h>
#include <sys/uio.h>

#include "vitadescriptor.h"
#include "vitaerror.h"
#include "fios2.h"

#ifndef SSIZE_MAX
/* ssize_t is not formally required to be the signed type
	 corresponding to size_t, but it is for all configurations supported
	 by glibc.  */
#if __WORDSIZE == 64 || __WORDSIZE32_SIZE_ULONG
#define SSIZE_MAX LONG_MAX
#else
#define SSIZE_MAX INT_MAX
#endif
#endif

static void ifree(char **ptrp) {
	free(*ptrp);
}

hidden ssize_t __readv(int fd, const struct iovec *vector, int count) {
	/* Find the total number of bytes to be read.  */
	size_t bytes = 0;
	for (int i = 0; i < count; ++i) {
		/* Check for ssize_t overflow.  */
		if (SSIZE_MAX - bytes < vector[i].iov_len) {
			return -1;
		}
		bytes += vector[i].iov_len;
	}

	char *buffer;
	char *malloced_buffer __attribute__((__cleanup__(ifree))) = NULL;

	malloced_buffer = buffer = (char *)malloc(bytes);
	if (buffer == NULL)
		return -1;

	/* Read the data.  */
	ssize_t bytes_read = read(fd, buffer, bytes);
	if (bytes_read < 0)
		return -1;

	/* Copy the data from BUFFER into the memory specified by VECTOR.  */
	bytes = bytes_read;
	for (int i = 0; i < count; ++i) {
		size_t copy = MIN(vector[i].iov_len, bytes);

		(void)memcpy((void *)vector[i].iov_base, (void *)buffer, copy);

		buffer += copy;
		bytes -= copy;
		if (bytes == 0)
			break;
	}

	return bytes_read;
}

ssize_t readv(int fd, const struct iovec *iov, int len) {
	int ret;
	int type = ERROR_GENERIC;
	DescriptorTranslation *fdmap = __fd_grab(fd);

	if (!fdmap) {
		if (0 > sceFiosFHTell(fd)) { // C++ is a bit weird
			errno = EBADF;
			return -1;
		}
		type = ERROR_FIOS;
		ret = sceFiosFHReadvSync(NULL, fd, iov, len);
	} else {
		switch (fdmap->type) {
		case VITA_DESCRIPTOR_TTY:
		case VITA_DESCRIPTOR_PIPE:
		case VITA_DESCRIPTOR_SOCKET:
			ret = __readv(fdmap->sce_uid, iov, len);
			break;
		case VITA_DESCRIPTOR_FILE:
			type = ERROR_FIOS;
			ret = sceFiosFHReadvSync(NULL, fdmap->sce_uid, iov, len);
			break;
		case VITA_DESCRIPTOR_DIRECTORY:
			ret = __make_sce_errno(EBADF);
			break;
		}

		__fd_drop(fdmap);
	}

	if (ret < 0) {
		if (ret != -1)
			errno = __sce_errno_to_errno(ret, type);
		return -1;
	}

	errno = 0;
	return ret;
}