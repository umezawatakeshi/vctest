/* 文字コードはＳＪＩＳ 改行コードはＣＲＬＦ */
/* $Id$ */

#include "stdafx.h"
#include "FlushCache.h"

char *pRandomBuf;
#define RANDOMBUFSIZE (16*1024*1024)

void InitFlushCache(void)
{
	pRandomBuf = (char *)VirtualAlloc(NULL, RANDOMBUFSIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	for (int i = 0; i < RANDOMBUFSIZE; i++)
		pRandomBuf[i] = rand();
}

void FlushCache(void)
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
