/* •¶šƒR[ƒh‚Í‚r‚i‚h‚r ‰üsƒR[ƒh‚Í‚b‚q‚k‚e */
/* $Id$ */

#pragma once

class CGuardedBuffer
{
private:
	size_t m_cbAllocated;
	size_t m_cbGuarded;
	char *m_pAllocated;
	char *m_pGuarded;

public:
	CGuardedBuffer(size_t cb);
	~CGuardedBuffer(void);

public:
	void *GetHeadGuardedBuffer(void);
	void *GetTailGuardedBuffer(size_t cb);
};
