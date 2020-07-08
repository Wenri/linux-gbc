#ifndef _WST_SE_KTRANS_H
#define _WST_SE_KTRANS_H

#include "wst_se_common_type.h"
#include "wst_se_define.h"


typedef struct tagSWCOMMUDATA
{
	unsigned short usFlags;
	unsigned short usInputLen;
	unsigned short usOutputLen;
	unsigned short usReserve;
	unsigned char *pucInbuf;
	unsigned char *pucOutbuf;
} SWCommuData;


struct tag_TRANS_PACKET
{
	int *piErrCode;
	PSECallBackfn pcallback;
	void* pParma;
	unsigned short usFlags;
	unsigned short usInputLen;
	unsigned short usOutputLen;
	unsigned char *pucInbuf;
	unsigned char *pucOutbuf;
	struct tag_dma_buf_ctl *pdmabufctl;
};

struct tag_USER_TRANS_PACKET
{
	struct tag_TRANS_PACKET *ptranspacket;
	PSECallBackfn pcallback;
	void* pParma;
	unsigned char *pucOutbuf;
};

int se_printk_hex(unsigned char *buff, int length);

ssize_t se_kernelwrite(
					unsigned char *pInPtr,
					unsigned short usInlen,
					unsigned char *pOutPtr,
					unsigned short *pusOutlen,
					unsigned char ucFlag,
					unsigned char *pucRetCode,
					PSECallBackfn pcallback,
					void* pParma
					);
#endif


