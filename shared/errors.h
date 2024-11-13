#ifndef RUNLUAU_ERRORS_H
#define RUNLUAU_ERRORS_H
#ifndef _WIN32
    #define ERROR_INVALID_PARAMETER EINVAL
	#define ERROR_INTERNAL_ERROR EIO
	#define ERROR_NOT_FOUND ENOENT
	#define ERROR_OUTOFMEMORY ENOMEM
#endif
#endif
