#pragma once

#ifdef __linux
	#define OutputDebugStringA(S) fputs(S,stderr)
#elif defined(__APPLE__)
	void OutputDebugStringA(const char *message)
	{
		fprintf(stderr, "%s", message);
	}
#endif
