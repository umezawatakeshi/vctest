/* •¶šƒR[ƒh‚Í‚r‚i‚h‚r ‰üsƒR[ƒh‚Í‚b‚q‚k‚e */
/* $Id$ */

#include "stdafx.h"
#include "FlushCache.h"
#include "intrin.h"

char *pRandomBuf;
#define RANDOMBUFSIZE (16*1024*1024)

bool bSupportCLFLUSHOPT = false;

void InitFlushCache(bool bOldStyle)
{
	if (bOldStyle)
	{
		pRandomBuf = (char*)VirtualAlloc(NULL, RANDOMBUFSIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		for (int i = 0; i < RANDOMBUFSIZE; i++)
			pRandomBuf[i] = rand();
	}
	else
	{
		int result[4];
		__cpuidex(result, 7, 0);
		bSupportCLFLUSHOPT = result[1] & (1 << 23);
	}
}

void OldFlushCache(void)
{
	static volatile char ch;
	DWORD_PTR dwpProcessAffinityMask, dwpSystemAffinityMask;

	for (DWORD_PTR mask = 1; mask != 0; mask <<= 1)
	{
		if (SetThreadAffinityMask(GetCurrentThread(), mask) != 0)
		{
			for (int i = 0; i < RANDOMBUFSIZE; i+=32)
				ch = pRandomBuf[i];
		}
	}

	GetProcessAffinityMask(GetCurrentProcess(), &dwpProcessAffinityMask, &dwpSystemAffinityMask);
	SetThreadAffinityMask(GetCurrentThread(), dwpProcessAffinityMask);
}

void FlushCache(const void* buf, size_t sz)
{
	if (sz == 0)
		return;

	const char* begin = (const char*)buf;
	const char* end = begin + sz;
	if (bSupportCLFLUSHOPT)
	{
		for (auto p = begin; p < end; p += 64 /* XXX */)
			_mm_clflushopt(p);
		_mm_clflushopt(end - 1);
	}
	else
	{
		for (auto p = begin; p < end; p += 64 /* XXX */)
			_mm_clflush(p);
		_mm_clflush(end - 1);
	}

	_mm_sfence();
}
