#pragma once

#ifdef __linux
#	include <stdio.h>
#	define PSD_SAMPLE_LOG(_message)		fputs(_message, stderr)
#elif defined(__APPLE__)
#	include <stdio.h>
#	define PSD_SAMPLE_LOG(_message)		fprintf(stderr, "%s", _message)
#elif defined(_WIN32)
#	define PSD_SAMPLE_LOG(_message)		OutputDebugStringA(_message)
#else
#	pragma error "Unknown platform"
#endif
