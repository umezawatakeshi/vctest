/* �����R�[�h�͂r�i�h�r ���s�R�[�h�͂b�q�k�e */
/* $Id$ */

#include "stdafx.h"
#include "GuardedBuffer.h"

#define GUARDSIZE (1024*1024)
#define ALLOCUNIT 4096

inline size_t ROUNDUP(size_t a, size_t b)
{
	return ((a + b - 1) / b) * b;
}

CGuardedBuffer::CGuardedBuffer(size_t cb, bool bHighAddress)
{
	DWORD dw;

	m_cbGuarded = ROUNDUP(cb, ALLOCUNIT);
	m_cbAllocated = m_cbGuarded + GUARDSIZE * 2;

	m_pAllocated = (char *)VirtualAlloc(NULL, m_cbAllocated, MEM_COMMIT | MEM_RESERVE | (bHighAddress ? MEM_TOP_DOWN : 0), PAGE_READWRITE);
	if (bHighAddress)
	{
#ifdef _WIN64
		if (((UINT_PTR)m_pAllocated) < 0x100000000)
#else
		if (((UINT_PTR)m_pAllocated) < 0x10000)
#endif
		{
			fprintf(stderr, "Cannot allocate buffer at high address\n");
			exit(1);
		}
	}
	m_pGuarded = m_pAllocated + GUARDSIZE;

	/*
	 * VirtualAlloc ����̓A�h���X��Ԃ��\�񂳂�Ă��邾���̏�Ԃ̂͂��Ȃ̂ŁA
	 * �v�����Ƀy�[�W�t�H���g���������ĉe�����󂯂�̂�������邽�߂ɁA
	 * �����ŏ������݂��s���Ď��ۂɎ������������蓖�Ă�����B
	 */
	for (volatile char* p = m_pGuarded; p < m_pGuarded + m_cbGuarded; p += 4096)
		*p = 0;

	VirtualProtect(m_pAllocated, GUARDSIZE, PAGE_NOACCESS, &dw);
	VirtualProtect(m_pGuarded + m_cbGuarded, GUARDSIZE, PAGE_NOACCESS, &dw);
}

CGuardedBuffer::~CGuardedBuffer(void)
{
	VirtualFree(m_pAllocated, m_cbAllocated, MEM_RELEASE);
}

void *CGuardedBuffer::GetHeadGuardedBuffer(void)
{
	return m_pGuarded;
}

void *CGuardedBuffer::GetTailGuardedBuffer(size_t cb)
{
	return m_pGuarded + (m_cbGuarded - cb);
}
