#include <linux/semaphore.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include "wst_se_ktrans.h"
#include "wst_se_echip_driver.h"
#include "wst_se_define.h"

struct task_struct *g_tasksend = NULL;
unsigned char *g_pCacheInBuf = NULL,*g_pCacheOutBuf=NULL;
static struct class *g_psecclass=NULL;
static struct device *g_psecdev=NULL;
static SECHIPDRV_CTRL *g_psechipDrvCtrl;
static int g_isechip_Major = -1;
static struct semaphore g_lowsema,g_sendsema,g_lowirqsema;
static atomic_t g_sendtotallen;
static struct tag_Queue_container g_RecQueueContainer;
static struct tag_Queue_container g_SendQueueContainer;
volatile int g_stopsend = 0;
static struct tag_dma_buf_ctl *g_pdmadatabuf;
static int g_iDmaBufNum = 0;
static struct work_struct g_recwork;
#define DRIVER_VERSION "01.02.200610"
DEFINE_SPINLOCK(g_reclistlock);
DEFINE_SPINLOCK(g_sendlistlock);
static irqreturn_t se_interrupt(int irq,void *p);
static irqreturn_t wst_low_channel_status(int irq,void *p);
static int globalmem_do_send_op(void *p) ;
static void globalmem_do_rec_op(struct work_struct *p);
static int g_iUseIntr=1;
module_param(g_iUseIntr,int,0644);

static int se_init_dma_buf(int idatasize, int idatanum)
{
	int i;
	struct tag_dma_buf_ctl *pdmactl;
	g_pdmadatabuf = kmalloc((sizeof(struct tag_dma_buf_ctl)*idatanum),GFP_KERNEL);
	if(!g_pdmadatabuf)
		return -1;
	for(i = 0; i < idatanum; i++)
	{
		pdmactl = &g_pdmadatabuf[i];
		clear_bit(0,(void*)&(pdmactl->Lockbyte));
		pdmactl->iInPool = 1;
		pdmactl->pDmaBuf = NULL;
		pdmactl->pDmaBuf = (unsigned char*)__get_free_page(GFP_KERNEL|GFP_DMA);
		if(!pdmactl->pDmaBuf)
		{
			g_iDmaBufNum = i;
			return SE_OK;
		}
	}
	g_iDmaBufNum = i;
	return SE_OK;
}

static int se_del_dma_buf(void)
{
	int i;
	struct tag_dma_buf_ctl *pdmactl;
	for(i = 0; i < g_iDmaBufNum; i++)
	{
		pdmactl = &g_pdmadatabuf[i];
		if(pdmactl)
		{
			free_page((unsigned long)pdmactl->pDmaBuf);
			pdmactl->pDmaBuf = NULL;
		}
	}
	kfree(g_pdmadatabuf);
	g_pdmadatabuf = NULL;
	return 0;
}

struct tag_dma_buf_ctl* se_get_dma_buf(int ikernel)
{
	struct tag_dma_buf_ctl* pbufctl = NULL;
	int i=0,ret;
	while(1)
	{
		pbufctl = &g_pdmadatabuf[i];
		ret=test_and_set_bit(0, (void*)&pbufctl->Lockbyte);
		if(ret)
		{
			pbufctl = NULL;
			i++;
			if(i == g_iDmaBufNum)
			{
				if(ikernel == 0)
				{
					schedule_timeout(HZ);
					i = 0;
				}
				else
					return pbufctl;
			}
		}
		else
		{
			return pbufctl;
		}
	}
}

int se_free_dma_buf(struct tag_dma_buf_ctl* pdmabufctl)
{
	if(!(test_bit(0,&pdmabufctl->Lockbyte)))
	{
		return -EINVAL;
	}

	clear_bit(0, &pdmabufctl->Lockbyte);
	return 0;
}

static unsigned long  bytes_align(struct device *pdev,unsigned long ulVirAddr)
{
	unsigned char diff;
	unsigned long ulPhyAddr = (unsigned long)__pa ((void*)ulVirAddr);
	if((ulPhyAddr & 0x000000000000003f) == 0)
		return ulVirAddr;
	diff = ((long)ulPhyAddr & (~(0x000000000000003f))) + 64 - ulPhyAddr;
	ulVirAddr += diff;
	return ulVirAddr;
}

static unsigned long  descri_bytes_align(unsigned long ulVirAddr)
{
	unsigned char diff;
	unsigned long ulPhyAddr = ulVirAddr;
	if((ulPhyAddr & (~0x00000000ffffffe0)) == 0)
		return ulVirAddr;
	diff = ((long)ulPhyAddr & 0x00000000ffffffe0) + 32 - ulPhyAddr;
	ulVirAddr += diff;
	return ulVirAddr;
}

int se_printk_hex(unsigned char *buff, int length)
{
	unsigned char *string_tmp = buff;
	int i;
	int count = 0;
	for(i = 0; i< length; i++, count++)
	{
		if(count < 16)
			printk("%02x ", string_tmp[i]);
		else
		{
			count = 0;
			printk("\n%02x ", string_tmp[i]);
			continue;
		}
	}
	printk("\n");
	return 0;
}

static int se_ChipInit(SECHIPDRV_CTRL *pDrvCtrl)
{
	dma_addr_t ulBusAddr;
	unsigned long ulVirAddr;
	int i = 0,j = 0;
	unsigned int dmaoldmask;

	for( i = 0; i < SWCHANNELNUM; i++)
	{
		ulVirAddr=(unsigned long)dma_alloc_coherent(
			pDrvCtrl->pdev,
			(SE_BDQUEUE_LEN * SE_BD_LENGTH+32),
			&ulBusAddr,GFP_KERNEL
			);
		if (ulVirAddr == 0 || ulBusAddr == 0)
		{
			return -EFAULT;
		}
		memset((void*)ulVirAddr,0,(SE_BDQUEUE_LEN*SE_BD_LENGTH));

		pDrvCtrl->ulBDMemBasePhy[i]   = ulBusAddr;
		pDrvCtrl->ulBDMemBase[i]      = ulVirAddr;
		pDrvCtrl->ulCurrBdReadPtr[i]  = 0;
		pDrvCtrl->ulCurrBdWritePtr[i] = 0;
		pDrvCtrl->ulCurrReadPtr[i]  = 0;
		pDrvCtrl->ulCurrWritePtr[i]= 0;
	}
	for(i = 0; i < SE_BDQUEUE_LEN;i++)
	{
		for(j = 0; j < SWCHANNELNUM; j++)
			(&((SE_BASIC_BD*)(pDrvCtrl->ulBDMemBase[j]))[i])->ucRetCode = 0x0f;
	}
	ulBusAddr = descri_bytes_align(pDrvCtrl->ulBDMemBasePhy[0]);
	HandleWrite32(pDrvCtrl, SE_HREG_BQBA0, HIULONG(ulBusAddr));
	HandleWrite32(pDrvCtrl, SE_LREG_BQBA0, LOULONG(ulBusAddr));
	HandleWrite32(pDrvCtrl, SE_REG_BQS0,  SE_BDQUEUE_LEN - 1);
	HandleWrite32(pDrvCtrl, SE_REG_RQRP0, pDrvCtrl->ulCurrBdReadPtr[0]);
	HandleWrite32(pDrvCtrl, SE_REG_BQWP0, pDrvCtrl->ulCurrBdWritePtr[0]);

	ulBusAddr = descri_bytes_align(pDrvCtrl->ulBDMemBasePhy[1]);
	HandleWrite32(pDrvCtrl, SE_HREG_BQBA1, HIULONG(ulBusAddr));
	HandleWrite32(pDrvCtrl, SE_LREG_BQBA1, LOULONG(ulBusAddr));
	HandleWrite32(pDrvCtrl, SE_REG_BQS1,  SE_BDQUEUE_LEN - 1);
	HandleWrite32(pDrvCtrl, SE_REG_RQRP1, pDrvCtrl->ulCurrBdReadPtr[1]);
	HandleWrite32(pDrvCtrl, SE_REG_BQWP1, pDrvCtrl->ulCurrBdWritePtr[1]);
	HandleRead32(pDrvCtrl, SE_REG_MSK, &dmaoldmask);
	HandleWrite32(pDrvCtrl, SE_REG_MSK, (dmaoldmask | DMA0_CTRL_CHANNEL_ENABLE | DMA1_CTRL_CHANNEL_ENABLE));
	if(g_iUseIntr != 0)
		HandleWrite32(pDrvCtrl, SE_LOWREG_INQ,1);
	else
		HandleWrite32(pDrvCtrl, SE_LOWREG_INQ,0);
	mdelay(1000);
	return SE_OK;
}

static void  se_ChipRelease(SECHIPDRV_CTRL *pDrvCtrl)
{
	int i;
	for(i = 0; i < SWCHANNELNUM; i++)
	{
		if(pDrvCtrl->ulBDMemBase[i])
		{
			dma_free_coherent(
				pDrvCtrl->pdev,
				(SE_BDQUEUE_LEN * SE_BD_LENGTH),
				(void*)pDrvCtrl->ulBDMemBase[i],
				pDrvCtrl->ulBDMemBasePhy[i]
				);
			pDrvCtrl->ulBDMemBase[i] = 0;
			pDrvCtrl->ulBDMemBasePhy[i] = 0;

		}
	}
}

static void SE_RESET(SECHIPDRV_CTRL *pdrvctl)
{

	unsigned int reg;
	unsigned long   ulreg64,uladdr=0x900000003ff00400;
	HandleRead32(pdrvctl, SE_REG_RESET, &reg);
	HandleWrite32(pdrvctl, SE_REG_RESET, reg|SE_DMA_CONTROL_RESET);
	mdelay(300);
	HandleWrite32(pdrvctl, SE_REG_RESET, (reg&(~SE_DMA_CONTROL_RESET))|SE_DMA_CONTROL_SET);
	mdelay(300);
	ulreg64 = readq((volatile void __iomem *)uladdr);
	if((ulreg64 & 0xf0000000000000) != 0xf0000000000000)
	{
		writeq(ulreg64|0xf0000000000000,(volatile void __iomem *)uladdr);
	}
	HandleWrite32(pdrvctl,SE_INT_CLR,0xf);
}

static int wst_init( void )
{
	int iRes=SE_OK;
	char cName[256];
	SECHIPDRV_CTRL *pdrvctl = NULL;
	if ((pdrvctl = kmalloc(sizeof(SECHIPDRV_CTRL), GFP_KERNEL)) == NULL)
	{
		return -ENOMEM;
	}
	memset(pdrvctl, 0, sizeof(SECHIPDRV_CTRL));
	pdrvctl->ulMemBase = 0x90000c0010200000;
	memset(cName, 0, 256);
	sema_init( &(pdrvctl->sema), 0);
	rwlock_init(&(pdrvctl->mr_lock));
	rwlock_init(&(pdrvctl->mr_lowlock));
	g_psechipDrvCtrl = pdrvctl;
	wst_InitQueue(&g_RecQueueContainer);
	wst_InitQueue(&g_SendQueueContainer);
	SE_RESET(pdrvctl);
	pdrvctl->iIrq = 1028;
	iRes = request_irq(pdrvctl->iIrq, &se_interrupt, IRQF_SHARED, "wst-se-hirq", pdrvctl);
	if(iRes)
	{
		printk("\nrequest_irq err");
		pdrvctl->iIrq = 0;
		goto err;
	}
	pdrvctl->ilowIrq = 1025;
	iRes = request_irq(pdrvctl->ilowIrq, &wst_low_channel_status, IRQF_SHARED, "wst-se-lirq", pdrvctl);
	if(iRes)
	{
		printk("\nrequest_lowirq err,iRes=0x%x\n",iRes);
		pdrvctl->ilowIrq = 0;
		goto err;
	}
	if ( SE_OK != se_ChipInit(pdrvctl) )
	{
		iRes = -ENODEV;
		goto err;
	}
	return SE_OK;
err:
	if (pdrvctl != NULL)
	{
		if (pdrvctl->ulMemBase)
		{
			iounmap ((void*)(pdrvctl->ulMemBase));
			pdrvctl->ulMemBase = 0;
		}
		if(pdrvctl->iIrq)
		{
			free_irq(pdrvctl->iIrq,pdrvctl);
			pdrvctl->iIrq = 0;
		}
		if(pdrvctl->ilowIrq)
		{
			free_irq(pdrvctl->ilowIrq,pdrvctl);
			pdrvctl->ilowIrq = 0;
		}

		se_ChipRelease(pdrvctl);
		kfree (pdrvctl);
		g_psechipDrvCtrl = NULL;
	}
	return iRes;
}

static void wst_clear(void)
{
	SECHIPDRV_CTRL * pdrvctl = NULL;
	pdrvctl = g_psechipDrvCtrl;
	if( pdrvctl )
	{
		if(pdrvctl->iIrq)
		{
			free_irq(pdrvctl->iIrq, pdrvctl);
			pdrvctl->iIrq = 0;
		}
		if(pdrvctl->ilowIrq)
		{
			free_irq(pdrvctl->ilowIrq,pdrvctl);
			pdrvctl->ilowIrq = 0;
		}
		se_ChipRelease(pdrvctl);
		if(pdrvctl->ulMemBase)
		{
			iounmap ((void*)pdrvctl->ulMemBase);
			pdrvctl->ulMemBase = 0;
		}
		kfree (pdrvctl);
		g_psechipDrvCtrl = NULL;
	}
	return;
}


static int globalmem_do_send_op(void *p)
{
	SE_BASIC_BD* pCurBD;
	unsigned int ulCurrWritePtr,ulWritePtr;
	unsigned short len = 0;
	unsigned long ulCurrAddrInput = 0,ulCurrAddrOutput = 0;
	SECHIPDRV_CTRL * pdrvctl;
	unsigned char *pInPtr;
	unsigned short usInlen;
	unsigned char *pOutPtr;
	unsigned short *pusOutlen;
	int iChannel;
	unsigned char ucFlag;
	unsigned char ucOpCode;
	unsigned char *pucRetCode;
	PSECallBackfn pcallback;
	void* pParma;
	int iKernel;
	struct completion *mycomplete;
	SEND_PACKAGE *psendpackage;
	unsigned long ulflag;
	while(1)
	{
PROG:
	down(&g_sendsema);
	if(g_stopsend == 1)
	{
		g_stopsend = 0;
		return 0;
	}
	spin_lock(&g_sendlistlock);
	psendpackage = (SEND_PACKAGE*)wst_Popfront_Que(&g_SendQueueContainer);
	if(!psendpackage)
	{
		spin_unlock(&g_sendlistlock);
		return 0;
	}
	spin_unlock(&g_sendlistlock);
	pdrvctl = psendpackage->pdrvctl;
	pInPtr = psendpackage->pInPtr;
	usInlen = psendpackage->usInlen;
	pOutPtr = psendpackage->pOutPtr;
	pusOutlen = psendpackage->pusOutlen;
	iChannel = psendpackage->iChannel;
	ucFlag = psendpackage->ucFlag;
	ucOpCode = psendpackage->ucOpCode;
	pucRetCode = psendpackage->pucRetCode;
	pcallback = psendpackage->pcallback;
	pParma = psendpackage->pParma;
	iKernel = psendpackage->iKernel;
	mycomplete = psendpackage->mycomplete;
	kfree(psendpackage);

LB_GETLOCK:
	if(iKernel == 0) {
		if((pdrvctl->ulCurrBdReadPtr[iChannel] == ((pdrvctl->ulCurrBdWritePtr[iChannel] + 1) & (SE_BDQUEUE_LEN-1)))
			|| ((atomic_read(&g_sendtotallen)+*pusOutlen+SE_FILL_LEN) >SE_MAX_SEND_LEN))
		{
			down_timeout( &(pdrvctl->sema),5*HZ);
			if(g_stopsend == 1)
			{
				g_stopsend = 0;
				return 0;
			}
			goto LB_GETLOCK;
		}
	}
	else {
		if((pdrvctl->ulCurrBdReadPtr[iChannel] == ((pdrvctl->ulCurrBdWritePtr[iChannel] + 1) & (SE_BDQUEUE_LEN-1)))
			|| ((atomic_read(&g_sendtotallen)+*pusOutlen+SE_FILL_LEN) >SE_MAX_SEND_LEN))
		{
			*pucRetCode = WST_SE_ERROR_FULL;
			if(pcallback)
				pcallback(pParma);
			goto PROG;
		}
	}
	ulCurrWritePtr = pdrvctl->ulCurrBdWritePtr[iChannel];
	ulWritePtr = (ulCurrWritePtr + 1) & (SE_BDQUEUE_LEN-1);

	pCurBD = &((SE_BASIC_BD*)(pdrvctl->ulBDMemBase[iChannel]))[ulCurrWritePtr];
	memset(pCurBD, 0x0, sizeof(SE_BASIC_BD));
	if(pcallback != NULL)
	{
		(pdrvctl->pcallback)[iChannel][ulCurrWritePtr] = pcallback;
		pdrvctl->pParma[iChannel][ulCurrWritePtr] = pParma;
	}
	else
	{
		(pdrvctl->pcallback)[iChannel][ulCurrWritePtr] = NULL;
		pdrvctl->pParma[iChannel][ulCurrWritePtr] = NULL;
	}

	pdrvctl->ikernel[iChannel][ulCurrWritePtr] = iKernel;
	pdrvctl->stsemphore[iChannel][ulCurrWritePtr] = mycomplete;

	if(pInPtr == pOutPtr)
	{
		if(pOutPtr)
		{
			len = usInlen >= *pusOutlen ? usInlen:*pusOutlen;
			if(len)
			{
				ulCurrAddrOutput = dma_map_single(pdrvctl->pdev, pOutPtr, len , DMA_BIDIRECTIONAL);
				if(ulCurrAddrOutput == 0)
				{
					TRACEERR("map ulCurrAddrOutput error\n");
					*pucRetCode = WST_SE_FAILURE;
					if(iKernel == 0) {
						complete(mycomplete);
					}
					else
					{
						*pucRetCode = WST_SE_FAILURE;
						if(pcallback)
							pcallback(pParma);
					}
					goto PROG;
				}
				pCurBD->ulOutputLPtr = LOULONG(ulCurrAddrOutput);
				pCurBD->ulOutputHPtr = HIULONG(ulCurrAddrOutput);
				pCurBD->ulInputLPtr = pCurBD->ulOutputLPtr;
				pCurBD->ulInputHPtr = pCurBD->ulOutputHPtr;
			}
		}
	}
	else
	{
		if(pOutPtr && (*pusOutlen))
		{
			ulCurrAddrOutput = dma_map_single(pdrvctl->pdev, pOutPtr, *pusOutlen, DMA_FROM_DEVICE);
			if(ulCurrAddrOutput == 0)
			{
				TRACEERR("map ulCurrAddrOutput error\n");
				*pucRetCode = WST_SE_FAILURE;
				if(iKernel == 0) {
					complete(mycomplete);
				}
				else
				{
					*pucRetCode = WST_SE_FAILURE;
					if(pcallback)
						pcallback(pParma);
				}
				goto PROG;
			}
			pCurBD->ulOutputLPtr = LOULONG(ulCurrAddrOutput);
			pCurBD->ulOutputHPtr = HIULONG(ulCurrAddrOutput);
		}
		if(usInlen && pInPtr)
		{
			ulCurrAddrInput = dma_map_single(pdrvctl->pdev, pInPtr, usInlen, DMA_TO_DEVICE);
			if(ulCurrAddrInput == 0)
			{
				if(ulCurrAddrOutput)
				{
					dma_unmap_single(pdrvctl->pdev,ulCurrAddrOutput,*pusOutlen,DMA_FROM_DEVICE);
					pCurBD->ulOutputLPtr = 0;
					pCurBD->ulOutputHPtr = 0;
				}
				*pucRetCode = WST_SE_FAILURE;
				if(iKernel == 0) {
					complete(mycomplete);
				}
				else
				{
					*pucRetCode = WST_SE_FAILURE;
					if(pcallback)
						pcallback(pParma);
				}
				goto PROG;
			}
			pCurBD->ulInputLPtr = LOULONG(ulCurrAddrInput);
			pCurBD->ulInputHPtr = HIULONG(ulCurrAddrInput);
		}
	}
	pCurBD->ucOpCode = ucOpCode & 0x0f;
	pCurBD->ucFlag = ucFlag & 0x7;
	pCurBD->usInputLength = usInlen;
	if(pusOutlen)
	{
		pCurBD->usOutputLength = *pusOutlen;
	}
	pCurBD->ucRetCode = 0x0f;

	pdrvctl->pusOutlen[iChannel][ulCurrWritePtr] = pusOutlen;
	pdrvctl->usInlen[iChannel][ulCurrWritePtr] = usInlen&0xffff;
	if(ulCurrAddrOutput)
		pdrvctl->ulOutputPtr[iChannel][ulCurrWritePtr] = (unsigned long*)ulCurrAddrOutput;
	else
		pdrvctl->ulOutputPtr[iChannel][ulCurrWritePtr] = 0;
	if(ulCurrAddrInput)
		pdrvctl->ulInputPtr[iChannel][ulCurrWritePtr] = (unsigned long*)ulCurrAddrInput;
	else
		pdrvctl->ulInputPtr[iChannel][ulCurrWritePtr] = 0;
	pdrvctl->pucRetCode[iChannel][ulCurrWritePtr] = pucRetCode;

	if(pdrvctl->pusOutlen[iChannel][ulCurrWritePtr])
		atomic_add((*(pdrvctl->pusOutlen[iChannel][ulCurrWritePtr])+SE_FILL_LEN),&g_sendtotallen);

	write_lock_irqsave(&(pdrvctl->mr_lock),ulflag);
	if(iChannel == 0)
		HandleWrite32(pdrvctl, SE_REG_BQWP0, ulWritePtr);
	else
		HandleWrite32(pdrvctl, SE_REG_BQWP1, ulWritePtr);
	write_unlock_irqrestore(&(pdrvctl->mr_lock),ulflag);
	pdrvctl->ulCurrBdWritePtr[iChannel] = ulWritePtr;
	}
	return 0;
}


static int se_hardtrans(
						SECHIPDRV_CTRL * pdrvctl,
						unsigned char *pInPtr,
						unsigned short usInlen,
						unsigned char *pOutPtr,
						unsigned short *pusOutlen,
						int iChannel,
						unsigned char ucFlag,
						unsigned char ucOpCode,
						unsigned char *pucRetCode,
						PSECallBackfn pcallback,
						void* pParma,
						int iKernel,
						struct completion *mycomplete
						)
{
	SEND_PACKAGE *psendpackage;
	psendpackage = kmalloc(sizeof(SEND_PACKAGE),GFP_KERNEL);
	if(NULL == psendpackage)
		return -1;
	psendpackage->pdrvctl = pdrvctl;
	psendpackage->pInPtr = pInPtr;
	psendpackage->usInlen = usInlen;
	psendpackage->pOutPtr = pOutPtr;
	psendpackage->pusOutlen = pusOutlen;
	psendpackage->iChannel = iChannel;
	psendpackage->ucFlag = ucFlag;
	psendpackage->ucOpCode = ucOpCode;
	psendpackage->pucRetCode = pucRetCode;
	psendpackage->pcallback = pcallback;
	psendpackage->pParma = pParma;
	psendpackage->iKernel = iKernel;
	psendpackage->mycomplete = mycomplete;
	spin_lock(&g_sendlistlock);
	wst_Pushback_Que(&g_SendQueueContainer,psendpackage);
	spin_unlock(&g_sendlistlock);
	up(&g_sendsema);
	return 0;
}


static irqreturn_t wst_low_channel_status(int irq,void *p)
{
	SECHIPDRV_CTRL *pdrvctl = (SECHIPDRV_CTRL*)p;
	int64_t  ulIntStat=0;
	unsigned long ulflag;
	read_lock_irqsave(&(pdrvctl->mr_lock),ulflag);
	HandleRead64(pdrvctl,SE_LOWREG_STS,&ulIntStat);
	if(ulIntStat == 2)
	{
		HandleWrite64(pdrvctl,SE_LOWINT_CLEAR,2);
		up(&g_lowirqsema);
	}
	read_unlock_irqrestore(&(pdrvctl->mr_lock),ulflag);
	return IRQ_HANDLED;
}


static int se_useropen (struct inode *inode, struct file *file)
{
	if(MINOR(inode->i_rdev) != 0)
	{
		return -ENODEV;
	}
	else
		return SE_OK;
}


static ssize_t wst_low_channel_userwrite_op(
											SECHIPDRV_CTRL * pdrvctl,
											SWCommuData *UserCommuData,
											int iskernel
											)
{
	unsigned long long addr = 0,outaddr = 0;
	int ilen;
	int count = SE_OK;
	unsigned long long ulsendlen;
	unsigned char *m_pCacheInBuf;
	unsigned char *m_pCacheOutBuf;
	unsigned long ulflag;
	if((g_pCacheInBuf == NULL) || (g_pCacheOutBuf == NULL))
		return -EFAULT;

	m_pCacheInBuf = (unsigned char*)bytes_align(0,(unsigned long)g_pCacheInBuf);
	m_pCacheOutBuf = (unsigned char*)bytes_align(0,(unsigned long)g_pCacheOutBuf);
	if(iskernel == 0)
	{
		if(wst_cpyusrbuf((void*)(UserCommuData->pucInbuf), (void*)m_pCacheInBuf, UserCommuData->usInputLen, READUBUF))
		{
			TRACEERR("copy user data error\n");
			return -EFAULT;
		}
	}
	else
		memcpy((void*)m_pCacheInBuf,(void*)(UserCommuData->pucInbuf),UserCommuData->usInputLen);
	ilen = UserCommuData->usInputLen >= UserCommuData->usOutputLen ? UserCommuData->usInputLen:UserCommuData->usOutputLen;
	addr = dma_map_single(pdrvctl->pdev,m_pCacheInBuf,ilen,DMA_TO_DEVICE);
	if(addr == 0)
	{
		TRACEERR("transfer buffer is err\n");
		return -EFAULT;
	}
	outaddr = dma_map_single(pdrvctl->pdev,m_pCacheOutBuf,ilen,DMA_FROM_DEVICE);
	if(outaddr == 0)
	{
		TRACEERR("transfer buffer is err\n");
		dma_unmap_single(pdrvctl->pdev,addr,ilen,DMA_TO_DEVICE);
		return -EFAULT;
	}
	ulsendlen = (UserCommuData->usInputLen/8);
	ulsendlen = (ulsendlen & 0x00000000ffffffff) << 32;
	write_lock_irqsave(&(pdrvctl->mr_lock),ulflag);
	HandleWrite64(pdrvctl, SE_WRITE_REG1, ulsendlen);
	HandleWrite64(pdrvctl, SE_WRITE_REG2, addr);
	HandleWrite64(pdrvctl, SE_WRITE_REG3, outaddr);
	write_unlock_irqrestore(&(pdrvctl->mr_lock),ulflag);
	if(g_iUseIntr != 0)
	{
		if(down_interruptible(&g_lowirqsema) == -EINTR)
		{
			count = -EINTR;
			goto EXIT;
		}
	}
	else
	{
		unsigned long start_jiffies = 0,end_jiffies = 0;
		int64_t  ulIntStat=0;
		start_jiffies = jiffies;
		end_jiffies = jiffies;
		while(1)
		{
			write_lock_irqsave(&(pdrvctl->mr_lock),ulflag);
			HandleRead64(pdrvctl,SE_LOWREG_SR,&ulIntStat);
			end_jiffies = jiffies;
			if(ulIntStat == 1)
			{
				HandleWrite64(pdrvctl,SE_LOWREG_SR,0);
				write_unlock_irqrestore(&(pdrvctl->mr_lock),ulflag);
				break;
			}
			write_unlock_irqrestore(&(pdrvctl->mr_lock),ulflag);
			if(jiffies_to_msecs(end_jiffies-start_jiffies)/1000 >= 90)
			{
				count = -EFAULT;
				goto EXIT;
			}
		}
	}
	dma_unmap_single(pdrvctl->pdev,addr,ilen,DMA_TO_DEVICE);
	dma_unmap_single(pdrvctl->pdev,outaddr,ilen,DMA_FROM_DEVICE);
	if(UserCommuData->usOutputLen)
	{
		if(iskernel == 0)
		{
			if(wst_cpyusrbuf(UserCommuData->pucOutbuf, m_pCacheOutBuf, UserCommuData->usOutputLen, WRITEUBUF))
			{
				return -EFAULT;
			}
		}
		else
			memcpy(UserCommuData->pucOutbuf,m_pCacheOutBuf,UserCommuData->usOutputLen);
	}
	return count;
EXIT:
	dma_unmap_single(pdrvctl->pdev,addr,ilen,DMA_TO_DEVICE);
	dma_unmap_single(pdrvctl->pdev,outaddr,ilen,DMA_FROM_DEVICE);
	return count;
}

static ssize_t se_userwrite(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	unsigned char *pCacheBuf = NULL, *pCacheOutBuf = NULL,*pCacheBufalign = NULL, *pCacheOutBufalign = NULL;
	SECHIPDRV_CTRL * pdrvctl = NULL;
	SWCommuData *pCommuData = NULL;
	int iCommuDatalen = 0;
	int pucRetCode = 0;
	unsigned short iChannel = 0;
	unsigned char ucFlag = 0, ucOpCode = 0;
	int *ppucRetCode;
	struct completion mycomplete;
	struct tag_dma_buf_ctl *pbufinctl=NULL,*pbufoutctl=NULL;
	int iret = 0;
	if(count == 0)
	{
		TRACEERR("count=0\n");
		return SE_OK;
	}
	if(MINOR(file->f_path.dentry->d_inode->i_rdev) != 0)
	{
		return -ENODEV;
	}
	iCommuDatalen = sizeof(SWCommuData);
	if (count != iCommuDatalen)
	{
		return -EINVAL;
	}
	pdrvctl = g_psechipDrvCtrl;
	pCommuData = kmalloc(iCommuDatalen, GFP_KERNEL);
	if(!pCommuData)
	{
		TRACEERR("pCommuData NULL\n");
		return -ENOMEM;
	}
	if(wst_cpyusrbuf((void*)buf, (void*)pCommuData, iCommuDatalen, READUBUF))
	{
		TRACEERR("copy user data error\n");
		count = -EFAULT;
		goto EXIT;
	}
	switch((pCommuData->usFlags)&0x000f)
	{
	case WST_GO_CHANNEL2:
		if((pCommuData->usInputLen > DMA_BUFSIZE) || (pCommuData->usOutputLen > DMA_BUFSIZE))
		{
			TRACEERR("len is error\n");
			count = -EINVAL;
			goto EXIT;
		}
		if(down_interruptible(&g_lowsema) == -EINTR)
		{
			count = -EINTR;
			goto EXIT;
		}
		count = wst_low_channel_userwrite_op(pdrvctl,pCommuData,0);
		up(&g_lowsema);
		goto EXIT;
	case WST_GO_CHANNEL0:
		if(pCommuData->usInputLen == 0)
		{
			count = -EINVAL;
			goto EXIT;
		}
		if(pCommuData->usInputLen != 0)
		{
			if(pCommuData->usInputLen > DMA_BUFSIZE)
			{
				count = -EINVAL;
				goto EXIT;
			}
			ucFlag = INPUT_VALID;
			if(pCommuData->usOutputLen)
				ucFlag |= OUTPUT_VALID;
		}

		iChannel = 0;
		ucOpCode = 0x0;
		break;
	case WST_GO_CHANNEL1:
		if(pCommuData->usInputLen == 0)
		{
			count = -EINVAL;
			goto EXIT;
		}
		if(pCommuData->usInputLen != 0)
		{
			if(pCommuData->usInputLen > DMA_BUFSIZE)
			{
				count = -EINVAL;
				goto EXIT;
			}
			ucFlag = INPUT_VALID;
			if(pCommuData->usOutputLen)
				ucFlag |= OUTPUT_VALID;
		}
		iChannel = 1;
		ucOpCode = 0x0;
		break;
	default:
		{
			count = -EINVAL;
			goto EXIT;
		}
	}
	if(pCommuData->pucInbuf)
	{
		if((pbufinctl = se_get_dma_buf(0)) == NULL)
		{
			TRACEERR("kmalloc pCacheBuf error\n");
			count = -ENOMEM;
			goto EXIT;
		}
		pCacheBuf = pbufinctl->pDmaBuf;
		pCacheBufalign = pCacheBuf;

		if(wst_cpyusrbuf((void*)(pCommuData->pucInbuf), (void*)pCacheBufalign, pCommuData->usInputLen, READUBUF))
		{
			TRACEERR("cpyuserbuf pCacheBufalign error\n");
			count = -ENOMEM;
			goto EXIT;
		}
	}
	if(pCommuData->pucOutbuf)
	{
		if((pbufoutctl = se_get_dma_buf(0)) == NULL)
		{
			TRACEERR("kmalloc pCacheOutBuf error\n");
			count = -ENOMEM;
			goto EXIT;
		}
		pCacheOutBuf = pbufoutctl->pDmaBuf;
		pCacheOutBufalign = pCacheOutBuf;
	}
	ppucRetCode = &pucRetCode;

	count = SE_OK;
	init_completion(&mycomplete);
	iret = se_hardtrans(
		pdrvctl,
		pCacheBufalign,
		pCommuData->usInputLen,
		pCacheOutBufalign,
		&(pCommuData->usOutputLen),
		iChannel,
		ucFlag,
		ucOpCode,
		(unsigned char*)ppucRetCode,
		0,
		0,
		0,
		&mycomplete
		);
	if(iret == -1)
	{
		count = -EIO;
		goto EXIT;
	}
	wait_for_completion(&mycomplete);
	if(pucRetCode != SE_OK)
	{
		count = -(SE_BASEERR+pucRetCode);
		goto EXIT;
	}

	if(pCommuData->pucOutbuf)
	{

		if(wst_cpyusrbuf(pCommuData->pucOutbuf, pCacheOutBufalign, pCommuData->usOutputLen, WRITEUBUF))
		{
			count = -EFAULT;
			goto EXIT;
		}
	}
EXIT:
	if(pbufinctl)
	{
		se_free_dma_buf(pbufinctl);
	}
	if(pbufoutctl)
	{
		se_free_dma_buf(pbufoutctl);
	}
	if(pCommuData)
		kfree(pCommuData);
	return count;
}
static void globalmem_do_rec_op(struct work_struct *p)
{
	INT_MESSAGE *intmessage;
	unsigned long ulflags1;
	while(1)
	{
		spin_lock_irqsave(&g_reclistlock,ulflags1);
		intmessage = (INT_MESSAGE*)wst_Popfront_Que(&g_RecQueueContainer);
		spin_unlock_irqrestore(&g_reclistlock,ulflags1);
		if(!intmessage)
		{
			return;
		}
		intmessage->pcallback(intmessage->pParma);
		kfree(intmessage);
	}
	return;
}
static irqreturn_t se_interrupt(int irq,void *p)
{
	SECHIPDRV_CTRL *pdrvctl;
	SE_BASIC_BD* pCurBD;
	unsigned int ulCurrReadPtr,ulReadPtr;
	int iChannel;
	int len = 0;
	int i;
	unsigned char ucMyRetCode = 0;
	unsigned long ulIntStat;
	int istatus[2] = {1,2};
	unsigned long ulflags;
	pdrvctl = (SECHIPDRV_CTRL*)p;
	if(!pdrvctl)
	{
		return IRQ_HANDLED;
	}
	read_lock_irqsave(&(pdrvctl->mr_lock),ulflags);
	HandleRead32(pdrvctl, SE_REG_STS, &ulIntStat);
	read_unlock_irqrestore(&(pdrvctl->mr_lock),ulflags);
	if((!(ulIntStat & INT_STAT_DMA_MASK)) || (ulIntStat == 0xffffffff))
	{
		return IRQ_HANDLED;
	}
	for(i = 0; i <= 1; i++)
	{
		if(ulIntStat & istatus[i])
		{
			if(i == 0)
			{
				read_lock_irqsave(&(pdrvctl->mr_lock),ulflags);
				HandleWrite32(pdrvctl,SE_INT_CLR,1);
				HandleRead32(pdrvctl, SE_REG_RQRP0, &ulReadPtr);
				read_unlock_irqrestore(&(pdrvctl->mr_lock),ulflags);
				iChannel = 0;
			}
			else
			{
				read_lock_irqsave(&(pdrvctl->mr_lock),ulflags);
				HandleWrite32(pdrvctl,SE_INT_CLR,2);
				HandleRead32(pdrvctl, SE_REG_RQRP1, &ulReadPtr);
				read_unlock_irqrestore(&(pdrvctl->mr_lock),ulflags);
				iChannel = 1;
			}
		}
		else
			continue;
		ulCurrReadPtr = pdrvctl->ulCurrReadPtr[iChannel];
		while(1)
		{
			if(ulCurrReadPtr != ulReadPtr)
			{
				pCurBD = &((SE_BASIC_BD*)(pdrvctl->ulBDMemBase[iChannel]))[ulCurrReadPtr];
				if(( pCurBD->ucRetCode == 0x0f) || ((pCurBD->ucFlag & 0x8) != 0x8))
				{
					continue;
				}
				else
				{
					if(pdrvctl->ulInputPtr[iChannel][ulCurrReadPtr] == pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr])
					{
						if(pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr])
						{
							len = (*(pdrvctl->pusOutlen[iChannel][ulCurrReadPtr])) >= pdrvctl->usInlen[iChannel][ulCurrReadPtr] ?
								(*(pdrvctl->pusOutlen[iChannel][ulCurrReadPtr])):pdrvctl->usInlen[iChannel][ulCurrReadPtr];
							dma_unmap_single(
								pdrvctl->pdev,
								(unsigned long)(pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr]),
								len,
								DMA_BIDIRECTIONAL
								);
							pCurBD->ulOutputLPtr = 0;
							pCurBD->ulOutputHPtr = 0;
							pCurBD->ulInputHPtr = 0;
							pCurBD->ulInputLPtr = 0;
							pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr] = 0;
						}
					}
					else
					{
						if(pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr])
						{
							dma_unmap_single(
								pdrvctl->pdev,
								(unsigned long)(pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr]),
								*(pdrvctl->pusOutlen[iChannel][ulCurrReadPtr]), DMA_FROM_DEVICE
								);
							smp_wmb();
							pdrvctl->ulOutputPtr[iChannel][ulCurrReadPtr] = 0;
						}
						if(pdrvctl->ulInputPtr[iChannel][ulCurrReadPtr])
						{
							dma_unmap_single(
								pdrvctl->pdev,
								(unsigned long)(pdrvctl->ulInputPtr[iChannel][ulCurrReadPtr]),
								pdrvctl->usInlen[iChannel][ulCurrReadPtr],
								DMA_TO_DEVICE
								);
							pdrvctl->ulInputPtr[iChannel][ulCurrReadPtr] = 0;
						}
					}
					ucMyRetCode = pCurBD->ucRetCode;
					memcpy(pdrvctl->pucRetCode[iChannel][ulCurrReadPtr],&ucMyRetCode,1);
					if(pCurBD->ucRetCode != SE_OK)
					{
						printk("\nstatus %x\n", pCurBD->ucRetCode);
					}
					if(pdrvctl->pusOutlen[iChannel][ulCurrReadPtr])
						atomic_sub(((*(pdrvctl->pusOutlen[iChannel][ulCurrReadPtr]))+SE_FILL_LEN),&g_sendtotallen);
					if((pdrvctl->ikernel)[iChannel][ulCurrReadPtr] != 0)
					{
						if(pdrvctl->pcallback[iChannel][ulCurrReadPtr])
						{
							INT_MESSAGE *intmessage=NULL;
							unsigned long ulflags1;
							intmessage = (INT_MESSAGE*)kmalloc(sizeof(INT_MESSAGE*),GFP_ATOMIC);
							if(!intmessage)
								return IRQ_HANDLED;
							intmessage->pcallback = pdrvctl->pcallback[iChannel][ulCurrReadPtr];
							intmessage->pParma = pdrvctl->pParma[iChannel][ulCurrReadPtr];
							spin_lock_irqsave(&g_reclistlock,ulflags1);
							wst_Pushback_Que(&g_RecQueueContainer,intmessage);
							spin_unlock_irqrestore(&g_reclistlock,ulflags1);
							schedule_work(&g_recwork);
						}
					}
					else
					{
						complete(pdrvctl->stsemphore[iChannel][ulCurrReadPtr]);
					}
					ulCurrReadPtr = ((ulCurrReadPtr + 1)&(SE_BDQUEUE_LEN - 1));
					pdrvctl->ulCurrReadPtr[iChannel] = ulCurrReadPtr;
					pdrvctl->ulCurrBdReadPtr[iChannel] = ulCurrReadPtr;
					if(pdrvctl->sema.count <= 0)
						up(&(pdrvctl->sema));
				}
			}
			else
				break;
		}
	}
	return IRQ_HANDLED;
}

static int se_userrelease(struct inode *inode, struct file *file)
{
	return SE_OK;
}

ssize_t se_kernelwrite(
					unsigned char *pInPtr,
					unsigned short usInlen,
					unsigned char *pOutPtr,
					unsigned short *pusOutlen,
					unsigned char ucFlag,
					unsigned char *pucRetCode,
					PSECallBackfn pcallback,
					void* pParma
					)
{
	int iret;
	SECHIPDRV_CTRL * pdrvctl;
	int iChannel;
	unsigned char ucOpCode;
	SWCommuData CommuData;

	pdrvctl = g_psechipDrvCtrl;

	switch(ucFlag)
	{
	case WST_GO_CHANNEL2:
		{
			CommuData.pucInbuf = pInPtr;
			CommuData.pucOutbuf = pOutPtr;
			CommuData.usFlags = 0;
			CommuData.usInputLen = usInlen;
			CommuData.usOutputLen = *pusOutlen;
			CommuData.usReserve = 0;
			if(down_interruptible(&g_lowsema) == -EINTR)
				return -EINTR;
			iret = wst_low_channel_userwrite_op(pdrvctl,&CommuData,1);
			up(&g_lowsema);
			return iret;
		}
	case WST_GO_CHANNEL0:
		if(pcallback == NULL)
			return WST_SE_PARAM_ERROR;
		if(usInlen == 0)
		{
			return -EINVAL;
		}
		ucFlag = 0;
		if(usInlen != 0)
		{
			if(usInlen > DMA_BUFSIZE)
			{
				return -EINVAL;
			}
			ucFlag = INPUT_VALID;
			if(*pusOutlen)
				ucFlag |= OUTPUT_VALID;
		}
		iChannel = 0;
		ucOpCode = 0x0;
		break;
	case WST_GO_CHANNEL1:
		if(pcallback == NULL)
			return WST_SE_PARAM_ERROR;
		if(usInlen == 0)
		{
			return -EINVAL;
		}
		ucFlag = 0;
		if(usInlen != 0)
		{
			if(usInlen > DMA_BUFSIZE)
			{
				return -EINVAL;
			}
			ucFlag = INPUT_VALID;
			if(*pusOutlen)
				ucFlag |= OUTPUT_VALID;
		}
		iChannel = 1;
		ucOpCode = 0x0;
		break;
	default:
		return -EINVAL;
	}
	iret = se_hardtrans(
		pdrvctl,
		pInPtr,
		usInlen,
		pOutPtr,
		pusOutlen,
		iChannel,
		ucFlag,
		ucOpCode,
		pucRetCode,
		pcallback,
		pParma,
		1,
		NULL
		);
	if(iret == -1)
	{
		return -EIO;
	}
	else
		return SE_OK;
}

static long sw_pci_userioctl(struct file *filp, u_int cmd, u_long arg)
{
	long iret = SE_OK;
	SECHIPDRV_CTRL *pdrvctl = g_psechipDrvCtrl;
	unsigned long ulvalue;
	HandleRead64(pdrvctl, 0x120, &ulvalue);
	printk("offset=0x%x,value=0x%lx\n",0x120,ulvalue);
	HandleRead64(pdrvctl, 0x118, &ulvalue);
	printk("offset=0x%x,value=0x%lx\n",0x118,ulvalue);
	return iret;
}


static struct file_operations SE_fops = {

	.owner = THIS_MODULE,
	.write = se_userwrite,
	.open = se_useropen,
	.release = se_userrelease,
	.unlocked_ioctl = sw_pci_userioctl,
	.compat_ioctl=sw_pci_userioctl
};

int  se_chip_load( void )
{
	int iRes = SE_OK;
	static u64 wst_dma_mask=DMA_BIT_MASK(64);
	if(g_isechip_Major >= 0)
		return WST_SE_HAS_OPEN;

	iRes = se_init_dma_buf(DMA_BUFSIZE,CTL_DMABUFNUM);
	if(iRes != SE_OK)
		return WST_SE_ERROR_MALLOC;
	g_psechipDrvCtrl = NULL;
	iRes = wst_init();
	if(iRes != SE_OK)
	{
		goto EXIT;
	}
	iRes = register_chrdev(0, CRYNAME, &SE_fops);
	if (iRes < 0)
	{
		goto EXIT;
	}
	g_isechip_Major = iRes;
	iRes = 0;
	g_psecclass = class_create(THIS_MODULE,CRYNAME);
	if (IS_ERR(g_psecclass)) {
		iRes = PTR_ERR(g_psecclass);
		goto EXIT;
	}

	g_psecdev = device_create(g_psecclass,NULL,MKDEV(g_isechip_Major,0),NULL,CRYNAME);
	if (IS_ERR(g_psecdev)) {
		iRes = PTR_ERR(g_psecdev);
		goto EXIT;
	}
	g_psechipDrvCtrl->pdev = g_psecdev;
	g_psechipDrvCtrl->pdev->dma_mask=&wst_dma_mask;

	sema_init(&g_lowsema, 1);
	sema_init(&g_sendsema, 0);
	sema_init(&g_lowirqsema, 0);
	atomic_set(&g_sendtotallen,0);
	g_pCacheInBuf = (unsigned char*)__get_free_page(GFP_KERNEL|GFP_DMA);
	if (IS_ERR(g_pCacheInBuf)) {
		iRes = PTR_ERR(g_pCacheInBuf);
		goto EXIT;
	}
	g_pCacheOutBuf = (unsigned char*)__get_free_page(GFP_KERNEL|GFP_DMA);
	if (IS_ERR(g_pCacheOutBuf)) {
		iRes = PTR_ERR(g_pCacheOutBuf);
		goto EXIT;
	}

	g_tasksend = kthread_create(globalmem_do_send_op,NULL,"g_tasksend");
	if(!IS_ERR(g_tasksend))
	{
		wake_up_process(g_tasksend);
	}
	else
	{
		g_tasksend = NULL;
		iRes = PTR_ERR(g_tasksend);
		goto EXIT;
	}
	INIT_WORK(&g_recwork, globalmem_do_rec_op);
	printk("this driver version is %s\n",DRIVER_VERSION);
	return SE_OK;
EXIT:
	se_del_dma_buf();
	if(g_tasksend)
		g_stopsend = 1;
	while(1)
	{
		if(g_stopsend == 0)
		{
			g_tasksend = NULL;
			break;
		}
		else
			continue;
	}
	if(g_pCacheInBuf)
	{

		free_page((unsigned long)g_pCacheInBuf);
		g_pCacheInBuf = NULL;
	}
	if(g_pCacheOutBuf)
	{

		free_page((unsigned long)g_pCacheOutBuf);
		g_pCacheOutBuf = NULL;
	}
	wst_clear();
	if(g_psecdev)
	{
		device_unregister(g_psecdev);
		g_psecdev = NULL;
	}
	if(g_psecclass)
	{
		class_destroy(g_psecclass);
		g_psecclass = NULL;
	}
	if(g_isechip_Major >= 0)
		unregister_chrdev(g_isechip_Major,CRYNAME);
	g_isechip_Major = -1;
	return iRes;
}

void se_chip_unload(void)
{
	SECHIPDRV_CTRL * pdrvctl = NULL;
	pdrvctl = g_psechipDrvCtrl;

	if(g_tasksend)
		g_stopsend = 1;
	up(&g_sendsema);
	up(&pdrvctl->sema);

	while(1)
	{
		if(g_stopsend == 0)
		{
			g_tasksend = NULL;
			break;
		}
		else
			continue;
	}
	if(g_pCacheInBuf)
	{

		free_page((unsigned long)g_pCacheInBuf);
		g_pCacheInBuf = NULL;
	}
	if(g_pCacheOutBuf)
	{

		free_page((unsigned long)g_pCacheOutBuf);
		g_pCacheOutBuf = NULL;
	}
	wst_clear();
	if(g_psecdev)
	{
		device_unregister(g_psecdev);
		g_psecdev = NULL;
	}
	if(g_psecclass)
	{
		class_destroy(g_psecclass);
		g_psecclass = NULL;
	}
	if(g_isechip_Major >= 0)
	{
		unregister_chrdev(g_isechip_Major,CRYNAME);
		g_isechip_Major = -1;
	}
	se_del_dma_buf();
}

static int __init initmodule(void)
{
	if((current_cpu_type() == CPU_LOONGSON3_COMP))
		return se_chip_load();
	else
		return -ENODEV;
}

static void __exit exitmodule(void)
{
	return se_chip_unload();
}


module_init(initmodule);
module_exit(exitmodule);

MODULE_AUTHOR("dcm");
MODULE_DESCRIPTION("se encryption chip driver Co westone");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
