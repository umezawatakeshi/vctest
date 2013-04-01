/* 文字コードはＳＪＩＳ 改行コードはＣＲＬＦ */
/* $Id$ */

/*
 * vctest.cpp
 * Copyright (C) 2008-2013  梅澤 威志
 * 
 * このプログラムには GNU GPLv2 を適用します。
 */

/*
 * 同梱されている getopt.c は NetBSD 由来のもの (src/lib/libc/stdlib/getopt.c 1.28) で、
 * Visual C++ でコンパイルできるように多少修正してあります。
 * getopt.c は BSD ライセンスが適用されます。
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <errno.h>
#include <vector>
#include <algorithm>

#include <windows.h>
#include <vfw.h>

using namespace std;

DWORD_PTR dwpProcessAffinityMask, dwpSystemAffinityMask;

extern "C" const char *getprogname(void)
{
	return __argv[0];
}
extern "C" int getopt(int argc, char * const argv[], const char *optstr);
extern "C" int opterr, optind, optopt, optreset;
extern "C" char *optarg;


void FCC2String(char *buf, DWORD fcc)
{
	sprintf(buf, "%c%c%c%c", (fcc&0xff), ((fcc>>8)&0xff), ((fcc>>16)&0xff), (fcc>>24));
}

HANDLE hFile;
vector<pair<LARGE_INTEGER, DWORD> > aviindex;
vector<double> enctime;
vector<double> dectime;
vector<bool> iskey;
double totalenctime = 0;
double totaldectime = 0;
LARGE_INTEGER liTotalOrigSize;
LARGE_INTEGER liTotalEncSize;


DWORD ScanSubChunk(void)
{
	DWORD name;
	DWORD len;
	DWORD cbRead;

	ReadFile(hFile, &name, 4, &cbRead, NULL);
	if (cbRead != 4)
		return 0;
	ReadFile(hFile, &len, 4, &cbRead, NULL);
	if (cbRead != 4)
		return 0;

	//printf("%08X %d\n", name, len);
	if (name == MAKEFOURCC('R','I','F','F') || name == MAKEFOURCC('L','I','S','T'))
	{
		DWORD type;
		DWORD curlen;

		ReadFile(hFile, &type, 4, &cbRead, NULL);
		if (cbRead != 4)
			return 8;

		curlen = 4;
		while (curlen < len)
			curlen += ScanSubChunk();
	}
	else
	{
		LARGE_INTEGER liZero;
		LARGE_INTEGER liLen;
		LARGE_INTEGER liCurPos;

		liZero.QuadPart = 0;
		liLen.QuadPart = ((len + 1) / 2) * 2;
		SetFilePointerEx(hFile, liZero, &liCurPos, FILE_CURRENT);
		SetFilePointerEx(hFile, liLen, NULL, FILE_CURRENT);
		if ((name & 0x0000ffff) == 0x3030)
			aviindex.push_back(pair<LARGE_INTEGER, DWORD>(liCurPos, len));
		len = liLen.LowPart;
	}
	return len + 8;
}

void ScanChunk(void)
{
	while (ScanSubChunk() > 0)
		/* NOTHING */;
}

char *pRandomBuf;
#define RANDOMBUFSIZE (16*1024*1024)
volatile char ch;

void InitFlushCache(void)
{
	pRandomBuf = (char *)VirtualAlloc(NULL, RANDOMBUFSIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	for (int i = 0; i < RANDOMBUFSIZE; i++)
		pRandomBuf[i] = rand();
}

void FlushCache(void)
{
	for (DWORD_PTR mask = 1; mask != 0; mask <<= 1)
	{
		if (SetThreadAffinityMask(GetCurrentThread(), mask) != 0)
		{
			for (int i = 0; i < RANDOMBUFSIZE; i+=32)
				ch = pRandomBuf[i];
		}
	}
	SetThreadAffinityMask(GetCurrentThread(), dwpProcessAffinityMask);
}

void usage(void)
{
	fprintf(stderr,
		"usage: %s [-cqv] [-a affinity_mask] [-f codec_fcc] <AVI file name>\n"
		"  -a affinity_mask  Set process affinity mask\n"
		"  -c                Enable lossless checking\n"
		"  -f codec_fourcc   Specify codec from command line\n"
		"  -q                Quiet output\n"
		"  -v                Verbose output\n"
		, getprogname());
	exit(1);
}

int main(int argc, char **argv)
{
	PAVISTREAM pStream;
	union
	{
		BITMAPINFOHEADER *pbmihOrig;
		void *pbmihOrigBuf;
	};
	union
	{
		BITMAPINFOHEADER *pbmihEncoded;
		void *pbmihEncodedBuf;
	};
	union
	{
		BITMAPINFOHEADER *pbmihDecoded;
		void *pbmihDecodedBuf;
	};
	LONG cbFormatOrig;
	LONG cbFormatEncoded;
	HIC hicCompress;
	HIC hicDecompress;
	DWORD dw;
	__declspec(align(4096)) static char bufOrig[1024*1024*12];
	__declspec(align(4096)) static char *bufEncoded = (char *)VirtualAlloc(NULL, 1024*1024*13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	VirtualProtect(bufEncoded + 1024*1024*12, 1024*1024, PAGE_NOACCESS, &dw);
	fprintf(stderr, "bufEncoded = %p bufEncoded-last = %p\n", bufEncoded, bufEncoded+1024*1024*12);
	__declspec(align(4096)) static char bufDecoded[1024*1024*12];
	LONG cbOrig;
	HRESULT hr;
	LARGE_INTEGER liStartEncode, liEndEncode;
	LARGE_INTEGER liStartDecode, liEndDecode;
	LARGE_INTEGER liFreq;
	DWORD dwAviIndexFlag;
	char buf[256];
	COMPVARS cv;
	BOOL b;
	DWORD dwHandler = -1;
	AVISTREAMINFO asi;
	DWORD cbRead;
	DWORD cbState = 0;
	void *pState;
	BOOL bCheckLossless = FALSE;
	int ch;
	int qopt = 0;
	int vopt = 0;

	while ((ch = getopt(argc, argv, "a:cf:qv")) != -1)
	{
		switch (ch)
		{
		case 'a':
			{
				unsigned long a = strtoul(optarg, NULL, 0);
				if (a == ULONG_MAX && errno == ERANGE)
				{
					fprintf(stderr, "%s: unknown affinity mask -- %s\n", getprogname(), optarg);
					usage();
					/* NOTREACHED */
				}
				GetProcessAffinityMask(GetCurrentProcess(), &dwpProcessAffinityMask, &dwpSystemAffinityMask);
				if ((dwpProcessAffinityMask & a) == 0)
				{
					fprintf(stderr, "%s: invalid affinity mask -- %s\n", getprogname(), optarg);
					usage();
					/* NOTREACHED */
				}
				fprintf(stderr, "changing process affinity mask from 0x%X to 0x%X\n", dwpProcessAffinityMask, dwpProcessAffinityMask & a);
				SetProcessAffinityMask(GetCurrentProcess(), dwpProcessAffinityMask & a);
			}
			break;
		case 'c':
			bCheckLossless = 1;
			break;
		case 'f':
			{
				if (strlen(optarg) != 4)
				{
					fprintf(stderr, "%s: bad fourcc -- %s\n", getprogname(), optarg);
					usage();
					/* NOTREACHED */
				}
				dwHandler = *(DWORD *)optarg;
			}
			break;
		case 'q':
			qopt++;
			break;
		case 'v':
			vopt++;
			break;
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
	{
		usage();
		/* NOTREACHED */
	}

	printf("lossless checking = %s\n", bCheckLossless ? "enabled" : "disabled");

	printf("\n");

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	liTotalOrigSize.QuadPart = 0;
	liTotalEncSize.QuadPart = 0;

	InitFlushCache();
	GetProcessAffinityMask(GetCurrentProcess(), &dwpProcessAffinityMask, &dwpSystemAffinityMask);
	QueryPerformanceFrequency(&liFreq);
	AVIFileInit();

	AVIStreamOpenFromFile(&pStream, argv[0], streamtypeVIDEO, 0, OF_SHARE_DENY_WRITE, NULL);
	hr = AVIStreamReadFormat(pStream, 0, NULL, &cbFormatOrig);
	if (FAILED(hr)) { printf("AVIStreamReadFormat() failed: %08X\n", hr); }
	pbmihOrigBuf = malloc(cbFormatOrig);
	hr = AVIStreamReadFormat(pStream, 0, pbmihOrig, &cbFormatOrig);
	if (FAILED(hr)) { printf("AVIStreamReadFormat() failed: %08X\n", hr); }
	AVIStreamInfo(pStream, &asi, sizeof(asi));
	printf("frame count       = %d\n", asi.dwLength);
	printf("frame rate        = %d/%d = %f fps\n", asi.dwRate, asi.dwScale, (double)asi.dwRate/(double)asi.dwScale);

	hFile = CreateFile(argv[0], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	FCC2String(buf, pbmihOrig->biCompression);
	printf("source fcc        = %s (%08X)\n", buf, pbmihOrig->biCompression);
	printf("source bitcount   = %d\n", pbmihOrig->biBitCount);

	ScanChunk();

	if (dwHandler == -1)
	{
		memset(&cv, 0, sizeof(cv));
		cv.cbSize = sizeof(cv);
		b = ICCompressorChoose(NULL, ICMF_CHOOSE_KEYFRAME, pbmihOrig, NULL, &cv, "vctest");
		if (!b)
		{
			printf("ICCompressorChoose() returned FALSE\n");
			return 0;
		}
		dwHandler = cv.fccHandler;
		cbState = ICGetStateSize(cv.hic);
		pState = malloc(cbState+1);
		ICGetState(cv.hic, pState, cbState);
		ICCompressorFree(&cv);

		FCC2String(buf, dwHandler);
		printf("codec fcc         = %s (%08X)\n", buf, dwHandler);
		printf("key frame rate    = %d\n", cv.lKey);
		printf("state size        = %d\n", cbState);
	}
	else
	{
		FCC2String(buf, dwHandler);
		printf("codec fcc         = %s (%08X)\n", buf, dwHandler);
	}

	hicCompress = ICOpen(ICTYPE_VIDEO, dwHandler, ICMODE_COMPRESS);
	if (hicCompress == NULL) { printf("ICOpen() failed\n"); }
	if (cbState > 0)
	{
		dw = ICSetState(hicCompress, pState, cbState);
		if (dw == 0) { printf("ICSetState() failed\n"); }
	}
	cbFormatEncoded = ICCompressGetFormatSize(hicCompress, pbmihOrig);
	pbmihEncodedBuf = malloc(cbFormatEncoded);
	dw = ICCompressGetFormat(hicCompress, pbmihOrig, pbmihEncoded);
	if (dw != ICERR_OK) { printf("ICCompressGetFormat() failed  dw=%08x\n", dw); }

	hicDecompress = ICOpen(ICTYPE_VIDEO, dwHandler, ICMODE_DECOMPRESS);
	if (hicDecompress == NULL) { printf("ICOpen() failed\n"); }
	pbmihDecodedBuf = malloc(cbFormatOrig);
	memcpy(pbmihDecoded, pbmihOrig, cbFormatOrig);

	dw = ICCompressBegin(hicCompress, pbmihOrig, pbmihEncoded);
	if (dw != ICERR_OK) { printf("ICCompressBegin() failed  dw=%08x\n", dw); }
	dw = ICDecompressBegin(hicDecompress, pbmihEncoded, pbmihDecoded);
	if (dw != ICERR_OK) { printf("ICDecompressBegin() failed  dw=%08x\n", dw); }

	for (int i = 0; i < asi.dwLength; i ++)
	{
		double etime, dtime;
		//hr = AVIStreamRead(pStream, i, 1, bufOrig, sizeof(bufOrig), &cbOrig, NULL);
		//if (FAILED(hr)) { printf("AVIStreamRead() failed: %08X\n", hr); }
		pair<LARGE_INTEGER, DWORD> idx = aviindex[i];
		b = SetFilePointerEx(hFile, idx.first, NULL, FILE_BEGIN);
		if (!b) { printf("SetFilePointerEx() failed  GetLastError=%08x\n", GetLastError()); break; }
		b = ReadFile(hFile, bufOrig, idx.second, &cbRead, NULL);
		if (!b) { printf("ReadFile() failed  GetLastError=%08x\n", GetLastError()); break; }
		FlushCache();
		QueryPerformanceCounter(&liStartEncode);
		dw = ICCompress(hicCompress, (i == 0 || (cv.lKey != 0 && (i % cv.lKey) == 0)) ? ICCOMPRESS_KEYFRAME : 0, pbmihEncoded, bufEncoded, pbmihOrig, bufOrig, NULL, &dwAviIndexFlag, i, sizeof(bufEncoded), 0, NULL, NULL);
		QueryPerformanceCounter(&liEndEncode);
		if (dw != ICERR_OK) { printf("ICCompress() failed  dw=%08x\n", dw); break; }
		char *pEncodedNew = bufEncoded;// + (1024*1024*12) - pbmihEncoded->biSizeImage;
		memmove(pEncodedNew, bufEncoded, pbmihEncoded->biSizeImage);
		FlushCache();
		QueryPerformanceCounter(&liStartDecode);
		dw = ICDecompress(hicDecompress, ((dwAviIndexFlag&AVIIF_KEYFRAME) ? 0 : ICDECOMPRESS_NOTKEYFRAME), pbmihEncoded, pEncodedNew, pbmihDecoded, bufDecoded);
		QueryPerformanceCounter(&liEndDecode);
		if (dw != ICERR_OK) { printf("ICDecompress() failed  dw=%08x\n", dw); break; }
		etime = (liEndEncode.QuadPart - liStartEncode.QuadPart) / (double)liFreq.QuadPart * 1000.0;
		dtime = (liEndDecode.QuadPart - liStartDecode.QuadPart) / (double)liFreq.QuadPart * 1000.0;
		if (!qopt)
		{
			printf("%5d/%5d %10.6fms %10.6fms %7dbytes (%4.1f%%) %s\n",
				i, asi.dwLength, etime, dtime,
				pbmihEncoded->biSizeImage, (double)pbmihEncoded->biSizeImage / (double)pbmihOrig->biSizeImage * 100.0,
				(pbmihEncoded->biSizeImage == 0) ? "NUL" : (dwAviIndexFlag&AVIIF_KEYFRAME) ? "KEY" : "");
		}
		if (bCheckLossless && memcmp(bufOrig, bufDecoded, pbmihOrig->biSizeImage) != 0)
		{
			printf("frame #%d NOT LOSSLESS!!! - aborting\n", i);
			break;
		}
		liTotalOrigSize.QuadPart += pbmihOrig->biSizeImage;
		liTotalEncSize.QuadPart += pbmihEncoded->biSizeImage;
		enctime.push_back(etime);
		dectime.push_back(dtime);
		iskey.push_back(dwAviIndexFlag&AVIIF_KEYFRAME);
		totalenctime += etime;
		totaldectime += dtime;
	}
	ICCompressEnd(hicCompress);
	ICDecompressEnd(hicDecompress);

	ICClose(hicCompress);
	ICClose(hicDecompress);
	AVIStreamRelease(pStream);

	AVIFileExit();

	printf("\n");

	printf("Size: %I64d/%I64d (%6.3f%%, %6.4f)\n",
		liTotalEncSize.QuadPart, liTotalOrigSize.QuadPart,
		(double)liTotalEncSize.QuadPart/(double)liTotalOrigSize.QuadPart*100.0,
		(double)liTotalOrigSize.QuadPart/(double)liTotalEncSize.QuadPart);

	vector<double> ratime;
	double totalratime = 0;

	for (int i = 0; i < asi.dwLength; i ++)
	{
		double t = 0;
		for (int j = i; j >= 0; j--)
		{
			t += dectime[j];
			if (iskey[j])
				break;
		}
		ratime.push_back(t);
		totalratime += t;
	}

	sort(enctime.begin(), enctime.end());
	sort(dectime.begin(), dectime.end());
	sort(ratime.begin(), ratime.end());

	printf("Encode time: %fms/%df = %fms/f = %f fps\n", totalenctime, asi.dwLength, totalenctime/asi.dwLength, 1000.0*asi.dwLength/totalenctime);
	if (vopt)
	{
		printf("    min  %f\n", enctime[0]);
		printf("    10%%  %f\n", enctime[enctime.size()*0.10]);
		printf("    25%%  %f\n", enctime[enctime.size()*0.25]);
		printf("    50%%  %f\n", enctime[enctime.size()*0.50]);
		printf("    75%%  %f\n", enctime[enctime.size()*0.75]);
		printf("    90%%  %f\n", enctime[enctime.size()*0.90]);
		printf("    max  %f\n", enctime[enctime.size()-1]);
	}

	printf("Decode time: %fms/%df = %fms/f = %f fps\n", totaldectime, asi.dwLength, totaldectime/asi.dwLength, 1000.0*asi.dwLength/totaldectime);
	if (vopt)
	{
		printf("    min  %f\n", dectime[0]);
		printf("    10%%  %f\n", dectime[dectime.size()*0.10]);
		printf("    25%%  %f\n", dectime[dectime.size()*0.25]);
		printf("    50%%  %f\n", dectime[dectime.size()*0.50]);
		printf("    75%%  %f\n", dectime[dectime.size()*0.75]);
		printf("    90%%  %f\n", dectime[dectime.size()*0.90]);
		printf("    max  %f\n", dectime[dectime.size()-1]);
	}

	printf("Random access time: %fms/f\n", totalratime/asi.dwLength);
	if (vopt)
	{
		printf("    min  %f\n", ratime[0]);
		printf("    10%%  %f\n", ratime[ratime.size()*0.10]);
		printf("    25%%  %f\n", ratime[ratime.size()*0.25]);
		printf("    50%%  %f\n", ratime[ratime.size()*0.50]);
		printf("    75%%  %f\n", ratime[ratime.size()*0.75]);
		printf("    90%%  %f\n", ratime[ratime.size()*0.90]);
		printf("    max  %f\n", ratime[ratime.size()-1]);
	}

	return 0;
}
