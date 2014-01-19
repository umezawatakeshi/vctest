/* 文字コードはＳＪＩＳ 改行コードはＣＲＬＦ */
/* $Id$ */

/*
 * Video Codec Test
 * Copyright (C) 2008-2013  梅澤 威志
 * 
 * このプログラムには GNU GPL バージョン2 またはそれ以降を適用します。
 */

/*
 * 同梱されている getopt.c は NetBSD 由来のもの (src/lib/libc/stdlib/getopt.c 1.28) で、
 * Visual C++ でコンパイルできるように多少修正してあります。
 * getopt.c は BSD ライセンスが適用されます。
 */

#include "stdafx.h"
#include "GuardedBuffer.h"
#include "FlushCache.h"

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

void usage(void)
{
	fprintf(stderr,
		"Video Codec Test, version " VERSION "\n"
		"Copyright (C) 2008-2013 UMEZAWA Takeshi\n"
		"Licensed under GNU General Public License version 2 or later.\n\n"
		"usage: %s [-cqvH] [-a affinity_mask] [-f codec_fcc] [-k key_frame_rate] [-s codec_state_hexstring] <AVI file name>\n"
		"  -a affinity_mask          Set process affinity mask\n"
		"  -c                        Enable lossless checking\n"
		"  -f codec_fourcc           Specify codec from command line\n"
		"  -k key_frame_rate         Specify key frame rate from command line\n"
		"  -s codec_state_hexstring  Specify codec state from command line\n"
		"  -H                        Allocate buffers at high address\n"
		"  -q                        Quiet output\n"
		"  -v                        Verbose output\n"
		, getprogname());
	exit(1);
}

DWORD dwHandler = -1;
BOOL bCheckLossless = FALSE;
bool Hopt = 0;
int qopt = 0;
int vopt = 0;
DWORD cbState = 0;
void *pState = NULL;
LARGE_INTEGER liFreq;
LONG nKeyFrameInterval = 0;
BOOL bStdOutConsole;

void ParseOption(int &argc, char **&argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "a:cf:qvs:k:H")) != -1)
	{
		switch (ch)
		{
		case 'a':
			{
				DWORD_PTR a;
				char *endptr;

				DWORD_PTR dwpProcessAffinityMask, dwpSystemAffinityMask;

				errno = 0;
#ifdef _WIN64
				a = _strtoui64(optarg, &endptr, 0);
#else
				a = strtoul(optarg, &endptr, 0);
#endif
				if (*endptr != '\0' || errno == ERANGE)
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
				if (!SetProcessAffinityMask(GetCurrentProcess(), dwpProcessAffinityMask & a))
				{
					fprintf(stderr, "%s: failed to set affinity mask -- %s\n", getprogname(), optarg);
					usage();
					/* NOTREACHED */
				}
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
		case 's':
			{
				if (strlen(optarg) % 2 != 0)
				{
					fprintf(stderr, "%s: bad hexstring -- %s\n", getprogname(), optarg);
					usage();
					/* NOTREACHED */
				}
				for (unsigned i = 0; i < strlen(optarg); i++)
				{
					if (!isxdigit((unsigned char)optarg[i]))
					{
						fprintf(stderr, "%s: bad hexstring -- %s\n", getprogname(), optarg);
						usage();
						/* NOTREACHED */
					}
				}
				if (pState != NULL)
					free(pState);
				cbState = strlen(optarg) / 2;
				pState = malloc(cbState + 1);
				for (unsigned i = 0; i < cbState; i++)
				{
					char buf[3];
					int val;
					buf[0] = optarg[i*2];
					buf[1] = optarg[i*2+1];
					buf[2] = '\0';
					sscanf(buf, "%x", &val);
					((char *)pState)[i] = val;
				}
			}
			break;
		case 'k':
			nKeyFrameInterval = atol(optarg);
			break;
		case 'H':
			Hopt = true;
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
}

void SelectCodec(const char *filename)
{
	PAVISTREAM pStream;
	HRESULT hr;
	BITMAPINFOHEADER *pbmihOrig;
	LONG cbFormatOrig;
	AVISTREAMINFO asi;
	char buf[8];

	hr = AVIStreamOpenFromFile(&pStream, filename, streamtypeVIDEO, 0, OF_SHARE_DENY_WRITE, NULL);
	if (FAILED(hr)) { printf("AVIStreamOpenFromFile() failed: %08X\n", hr); }
	hr = AVIStreamReadFormat(pStream, 0, NULL, &cbFormatOrig);
	if (FAILED(hr)) { printf("AVIStreamReadFormat() failed: %08X\n", hr); }
	pbmihOrig = (BITMAPINFOHEADER *)malloc(cbFormatOrig);
	hr = AVIStreamReadFormat(pStream, 0, pbmihOrig, &cbFormatOrig);
	if (FAILED(hr)) { printf("AVIStreamReadFormat() failed: %08X\n", hr); }
	AVIStreamInfo(pStream, &asi, sizeof(asi));

	if (dwHandler == -1)
	{
		COMPVARS cv;

		memset(&cv, 0, sizeof(cv));
		cv.cbSize = sizeof(cv);
		if (!ICCompressorChoose(NULL, ICMF_CHOOSE_KEYFRAME, pbmihOrig, NULL, &cv, "vctest"))
		{
			printf("ICCompressorChoose() returned FALSE\n");
			exit(1);
		}
		dwHandler = cv.fccHandler;
		cbState = ICGetStateSize(cv.hic);
		pState = malloc(cbState+1);
		ICGetState(cv.hic, pState, cbState);
		nKeyFrameInterval = cv.lKey;
		ICCompressorFree(&cv);
	}

	FCC2String(buf, dwHandler);
	printf("codec fcc         = %s (%08X)\n", buf, dwHandler);
	printf("key frame rate    = %ld\n", nKeyFrameInterval);
	printf("state size        = %u\n", cbState);
	printf("state data        =");
	for (unsigned i = 0; i < cbState; i++)
		printf(" %02X", ((char *)pState)[i]);
	printf("\n");

	free(pbmihOrig);
}

void BenchmarkCodec(const char *filename)
{
	PAVISTREAM pStream;
	HRESULT hr;
	BITMAPINFOHEADER *pbmihOrig;
	BITMAPINFOHEADER *pbmihEncoded;
	BITMAPINFOHEADER *pbmihDecoded;
	LONG cbFormatOrig;
	LONG cbFormatEncoded;
	AVISTREAMINFO asi;
	HIC hicCompress;
	HIC hicDecompress;
	char buf[8];
	LARGE_INTEGER liStartEncode, liEndEncode;
	LARGE_INTEGER liStartDecode, liEndDecode;
	DWORD dw;
	LRESULT lr;
	unsigned __int64 cbOrigTotal = 0;
	unsigned __int64 cbEncodedTotal = 0;
	double totalenctime = 0;
	double totaldectime = 0;

	printf("\n");

	aviindex.clear();

	printf("file name         = %s\n", filename);
	hr = AVIStreamOpenFromFile(&pStream, filename, streamtypeVIDEO, 0, OF_SHARE_DENY_WRITE, NULL);
	if (FAILED(hr)) { printf("AVIStreamOpenFromFile() failed: %08X\n", hr); }
	hr = AVIStreamReadFormat(pStream, 0, NULL, &cbFormatOrig);
	if (FAILED(hr)) { printf("AVIStreamReadFormat() failed: %08X\n", hr); }
	pbmihOrig = (BITMAPINFOHEADER *)malloc(cbFormatOrig);
	hr = AVIStreamReadFormat(pStream, 0, pbmihOrig, &cbFormatOrig);
	if (FAILED(hr)) { printf("AVIStreamReadFormat() failed: %08X\n", hr); }
	AVIStreamInfo(pStream, &asi, sizeof(asi));
	printf("frame count       = %d\n", asi.dwLength);
	printf("frame rate        = %d/%d = %f fps\n", asi.dwRate, asi.dwScale, (double)asi.dwRate/(double)asi.dwScale);

	vector<double> enctime(asi.dwLength);
	vector<double> dectime(asi.dwLength);
	vector<bool> iskey(asi.dwLength);

	FCC2String(buf, pbmihOrig->biCompression);
	printf("source fcc        = %s (%08X)\n", buf, pbmihOrig->biCompression);
	printf("source bitcount   = %d\n", pbmihOrig->biBitCount);

	printf("\n");

	CGuardedBuffer bufOrig(pbmihOrig->biSizeImage, Hopt);
	CGuardedBuffer bufDecoded(pbmihOrig->biSizeImage, Hopt);

	hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	ScanChunk();

	hicCompress = ICOpen(ICTYPE_VIDEO, dwHandler, ICMODE_COMPRESS);
	if (hicCompress == NULL) { printf("ICOpen() failed\n"); }
	if (cbState > 0)
	{
		lr = ICSetState(hicCompress, pState, cbState);
		if (lr == 0) { printf("ICSetState() failed\n"); }
	}
	cbFormatEncoded = ICCompressGetFormatSize(hicCompress, pbmihOrig);
	pbmihEncoded = (BITMAPINFOHEADER *)malloc(cbFormatEncoded);
	lr = ICCompressGetFormat(hicCompress, pbmihOrig, pbmihEncoded);
	if (lr != ICERR_OK) { printf("ICCompressGetFormat() failed  lr=%p\n", lr); }

	size_t cbEncodedBuf = ICCompressGetSize(hicCompress, pbmihOrig, pbmihEncoded);

	CGuardedBuffer bufEncoded(cbEncodedBuf, Hopt);

	hicDecompress = ICOpen(ICTYPE_VIDEO, dwHandler, ICMODE_DECOMPRESS);
	if (hicDecompress == NULL) { printf("ICOpen() failed\n"); }
	pbmihDecoded = (BITMAPINFOHEADER *)malloc(cbFormatOrig);
	memcpy(pbmihDecoded, pbmihOrig, cbFormatOrig);

	lr = ICCompressBegin(hicCompress, pbmihOrig, pbmihEncoded);
	if (lr != ICERR_OK) { printf("ICCompressBegin() failed  lr=%p\n", lr); }
	lr = ICDecompressBegin(hicDecompress, pbmihEncoded, pbmihDecoded);
	if (lr != ICERR_OK) { printf("ICDecompressBegin() failed  lr=%p\n", lr); }

	for (unsigned int i = 0; i < asi.dwLength; i ++)
	{
		double etime, dtime;
		DWORD cbRead;
		DWORD dwAviIndexFlag;

		if (bStdOutConsole)
			printf("\r");
		if (!qopt || bStdOutConsole)
			printf("%5d/%5d", i, asi.dwLength);
		fflush(stdout);

		//hr = AVIStreamRead(pStream, i, 1, bufOrig, sizeof(bufOrig), &cbOrig, NULL);
		//if (FAILED(hr)) { printf("AVIStreamRead() failed: %08X\n", hr); }
		pair<LARGE_INTEGER, DWORD> idx = aviindex[i];
		if (!SetFilePointerEx(hFile, idx.first, NULL, FILE_BEGIN)) { printf("SetFilePointerEx() failed  GetLastError=%08x\n", GetLastError()); break; }
		if (!ReadFile(hFile, bufOrig.GetHeadGuardedBuffer(), idx.second, &cbRead, NULL)) { printf("ReadFile() failed  GetLastError=%08x\n", GetLastError()); break; }

		FlushCache();
		QueryPerformanceCounter(&liStartEncode);
		dw = ICCompress(hicCompress, (i == 0 || (nKeyFrameInterval != 0 && (i % nKeyFrameInterval) == 0)) ? ICCOMPRESS_KEYFRAME : 0, pbmihEncoded, bufEncoded.GetHeadGuardedBuffer(), pbmihOrig, bufOrig.GetHeadGuardedBuffer(), NULL, &dwAviIndexFlag, i, sizeof(bufEncoded), 0, NULL, NULL);
		QueryPerformanceCounter(&liEndEncode);
		if (dw != ICERR_OK) { printf("ICCompress() failed  dw=%08x\n", dw); break; }

		FlushCache();
		QueryPerformanceCounter(&liStartDecode);
		dw = ICDecompress(hicDecompress, ((dwAviIndexFlag&AVIIF_KEYFRAME) ? 0 : ICDECOMPRESS_NOTKEYFRAME), pbmihEncoded, bufEncoded.GetHeadGuardedBuffer(), pbmihDecoded, bufDecoded.GetHeadGuardedBuffer());
		QueryPerformanceCounter(&liEndDecode);
		if (dw != ICERR_OK) { printf("ICDecompress() failed  dw=%08x\n", dw); break; }

		etime = (liEndEncode.QuadPart - liStartEncode.QuadPart) / (double)liFreq.QuadPart * 1000.0;
		dtime = (liEndDecode.QuadPart - liStartDecode.QuadPart) / (double)liFreq.QuadPart * 1000.0;
		if (!qopt)
		{
			printf(" %10.6fms %10.6fms %7dbytes (%4.1f%%) %s\n",
				etime, dtime,
				pbmihEncoded->biSizeImage, (double)pbmihEncoded->biSizeImage / (double)pbmihOrig->biSizeImage * 100.0,
				(pbmihEncoded->biSizeImage == 0) ? "NUL" : (dwAviIndexFlag&AVIIF_KEYFRAME) ? "KEY" : "");
		}
		if (bCheckLossless && memcmp(bufOrig.GetHeadGuardedBuffer(), bufDecoded.GetHeadGuardedBuffer(), pbmihOrig->biSizeImage) != 0)
		{
			printf("frame #%d NOT LOSSLESS!!! - aborting\n", i);
			break;
		}
		cbOrigTotal += pbmihOrig->biSizeImage;
		cbEncodedTotal += pbmihEncoded->biSizeImage;
		enctime[i] = etime;
		dectime[i] = dtime;
		iskey[i] = ((dwAviIndexFlag&AVIIF_KEYFRAME) != 0);
		totalenctime += etime;
		totaldectime += dtime;
	}
	ICCompressEnd(hicCompress);
	ICDecompressEnd(hicDecompress);

	ICClose(hicCompress);
	ICClose(hicDecompress);
	AVIStreamRelease(pStream);

	if (!qopt)
		printf("\n");
	else if (bStdOutConsole)
		printf("\r");

	printf("Size: %I64d/%I64d (%6.3f%%, %6.4f)\n",
		cbEncodedTotal, cbOrigTotal,
		(double)cbEncodedTotal/(double)cbOrigTotal*100.0,
		(double)cbOrigTotal/(double)cbEncodedTotal);

	vector<double> ratime(asi.dwLength);
	double totalratime = 0;

	for (unsigned int i = 0; i < asi.dwLength; i ++)
	{
		double t = 0;
		for (int j = i; j >= 0; j--)
		{
			t += dectime[j];
			if (iskey[j])
				break;
		}
		ratime[i] = t;
		totalratime += t;
	}

	sort(enctime.begin(), enctime.end());
	sort(dectime.begin(), dectime.end());
	sort(ratime.begin(), ratime.end());

	printf("Encode time: %fms/%df = %fms/f = %f fps\n", totalenctime, asi.dwLength, totalenctime/asi.dwLength, 1000.0*asi.dwLength/totalenctime);
	if (vopt)
	{
		printf("    min  %f\n",  enctime[0]);
		printf("    10%%  %f\n", enctime[(size_t)(enctime.size()*0.10)]);
		printf("    25%%  %f\n", enctime[(size_t)(enctime.size()*0.25)]);
		printf("    50%%  %f\n", enctime[(size_t)(enctime.size()*0.50)]);
		printf("    75%%  %f\n", enctime[(size_t)(enctime.size()*0.75)]);
		printf("    90%%  %f\n", enctime[(size_t)(enctime.size()*0.90)]);
		printf("    max  %f\n",  enctime[(size_t)(enctime.size()-1)]);
	}

	printf("Decode time: %fms/%df = %fms/f = %f fps\n", totaldectime, asi.dwLength, totaldectime/asi.dwLength, 1000.0*asi.dwLength/totaldectime);
	if (vopt)
	{
		printf("    min  %f\n",  dectime[0]);
		printf("    10%%  %f\n", dectime[(size_t)(dectime.size()*0.10)]);
		printf("    25%%  %f\n", dectime[(size_t)(dectime.size()*0.25)]);
		printf("    50%%  %f\n", dectime[(size_t)(dectime.size()*0.50)]);
		printf("    75%%  %f\n", dectime[(size_t)(dectime.size()*0.75)]);
		printf("    90%%  %f\n", dectime[(size_t)(dectime.size()*0.90)]);
		printf("    max  %f\n",  dectime[(size_t)(dectime.size()-1)]);
	}

	printf("Random access time: %fms/f\n", totalratime/asi.dwLength);
	if (vopt)
	{
		printf("    min  %f\n",  ratime[0]);
		printf("    10%%  %f\n", ratime[(size_t)(ratime.size()*0.10)]);
		printf("    25%%  %f\n", ratime[(size_t)(ratime.size()*0.25)]);
		printf("    50%%  %f\n", ratime[(size_t)(ratime.size()*0.50)]);
		printf("    75%%  %f\n", ratime[(size_t)(ratime.size()*0.75)]);
		printf("    90%%  %f\n", ratime[(size_t)(ratime.size()*0.90)]);
		printf("    max  %f\n",  ratime[(size_t)(ratime.size()-1)]);
	}
}

int main(int argc, char **argv)
{
	DWORD dw;

	bStdOutConsole = GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dw); // 正常終了すればコンソールのはず

	ParseOption(argc, argv);

	if (argc < 1)
	{
		usage();
		/* NOTREACHED */
	}

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

	InitFlushCache();
	GetProcessAffinityMask(GetCurrentProcess(), &dwpProcessAffinityMask, &dwpSystemAffinityMask);
	QueryPerformanceFrequency(&liFreq);
	AVIFileInit();

	SelectCodec(argv[0]);

	for (int i = 0; i < argc; i++)
		BenchmarkCodec(argv[i]);

	AVIFileExit();

	return 0;
}
