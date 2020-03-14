/* 文字コードはＳＪＩＳ 改行コードはＣＲＬＦ */
/* $Id$ */

#include "stdafx.h"
#include "FlushCache.h"
#include "intrin.h"

char *pRandomBuf;
#define RANDOMBUFSIZE (16*1024*1024)

bool bSupportCLFLUSHOPT = false;
size_t cbStrideCLFLUSH = 64;

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
		__cpuidex(result, 1, 0);
		cbStrideCLFLUSH = ((result[1] >> 8) & 0xff) * 8;
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

	/*
	 * Intel SDE 8.35.0 にはバグがあって、
	 * CLFLUSH で指定したアドレスから 64 バイトアクセス可能でないとアクセス違反を起こすので、
	 * アドレスを整列させて CLFLUSH する。
	 */
	const char* begin = (const char*)buf;
	const char* end = begin + sz - 1;
	begin = (const char*)((uintptr_t)begin & ~(uintptr_t)(cbStrideCLFLUSH - 1));
	end   = (const char*)((uintptr_t)end   & ~(uintptr_t)(cbStrideCLFLUSH - 1));

	if (bSupportCLFLUSHOPT)
	{
		for (auto p = begin; p <= end; p += cbStrideCLFLUSH)
			_mm_clflushopt(p);
	}
	else
	{
		for (auto p = begin; p <= end; p += cbStrideCLFLUSH)
			_mm_clflush(p);
	}

	_mm_sfence();
}
