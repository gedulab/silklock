// BoxBoss.cpp: implementation of the CBoxBoss class.
//
//////////////////////////////////////////////////////////////////////

#include "boxboss.h"
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>

#define GE_USE_C_JMP

void d4d(const char * format, ...);
#define Msg d4d
#define _T(s) s
jmp_buf calc_safe_env;

CBoxBoss::CBoxBoss()
{
	m_pSums = NULL;
	m_bNeedNotify = false;
	m_dwCalcThreads = 0;
	m_pMasterThread = nullptr;
}

CBoxBoss::~CBoxBoss()
{
	if(m_pSums!=NULL)
		Cleanup();
}

int CBoxBoss::Setup(ChairMan * pChairMan)
{
	m_pChairMan = pChairMan;
	return 0;
}
bool CBoxBoss::GetSettings(ADVSETTING * pSettings)
{
	return false; // m_pChairManDlg->GetSettings(pSettings);
}
void CBoxBoss::Notify(int thread_id, int notice)
{
	
#ifdef 	GE_USE_C_JMP	
	m_pChairMan->Notify(thread_id, notice);
#else
	if(m_pChairMan == NULL)
		throw 0xbadfeed;
#endif
}	
double GetMember(int n, double x)
{
	double numerator = 1;
	for( int i=0; i<n; i++ )
		numerator = numerator*x;

	if ( n % 2 == 0 )
		return ( - numerator / n );
	else
		return numerator/n;
}
static void calc_signal_handler(int sig)
{
    d4d("catcha: received signal %d: ", sig);
    if(sig == SIGSEGV)
    {
       d4d("calc_signal_handler: SEGMENT FAULT");
       longjmp(calc_safe_env, 1);
    }
    else if(sig == SIGINT)
       d4d("calc_signal_handler:INTERRUPTED, by CTRL +C?");
    else
       d4d("calc_signal_handler: Not translated signal");
}

uint32_t ParallelCalc(PTHREADPARAM pParam)
{
	int nThreadIndex = pParam->dwIndex;
	int nNumThreads = pParam->pBoss->m_dwCalcThreads;
	int nSeriesMemberCount = pParam->pBoss->m_dwSeriesMemberCount;
	double x = pParam->pBoss->m_dXtoCalc;
	uint32_t dwWait;

	d4d(_T("Calc thread %d come to work..."), nThreadIndex);
	
	pParam->pBoss->m_pSums[nThreadIndex] = 0;
	for(int i=nThreadIndex; i<nSeriesMemberCount; i+=nNumThreads)
	{
		pParam->pBoss->m_pSums[nThreadIndex] += GetMember(i+1, x);
	}
	//Signal Master thread that one more processing thread is done
	pParam->pBoss->m_CountLock.lock();
	pParam->pBoss->m_dwThreadCount++;
	dwWait = 100000;
#ifdef _WIN32
	Sleep(1500);
#else
    sleep(1); // sleep a while to emulate slowness behavior
#endif	

    if(pParam->pBoss->m_bNeedNotify)
	{
		pParam->pBoss->m_nDoorBells[nThreadIndex] = 1;
		pParam->pBoss->Notify(nThreadIndex, 1);
	}
	pParam->pBoss->m_CountLock.unlock();

	d4d(_T("Thread %d is done and quitting..."),nThreadIndex);
	return 0;
}
uint32_t CalcThreadProc(void* p)
{
	uint32_t ret = 0;
	PTHREADPARAM pParam=(PTHREADPARAM)p;
#ifdef GE_USE_C_JMP	
	signal(SIGSEGV, calc_signal_handler);
	signal(SIGINT, calc_signal_handler);
	
	int rp = setjmp(calc_safe_env);
	if(rp != 0){
		d4d("bad exception occurred, log and quit");
		return -1;
	}
#endif	
	try {
		ParallelCalc(pParam);
	} catch(...) {
		d4d("bad exception occurred, log and quit");
		ret = -2;
	};
	return ret;
}

uint32_t MasterThreadProc(void* p)
{
	CBoxBoss * pBoss=(CBoxBoss*)p;
	uint32_t i;
	d4d(_T("Master thread entered..."));

	while (pBoss->m_dwThreadCount != pBoss->m_dwCalcThreads)
	{
        ///Sleep(1);
	}   // busy wait until all threads are done with computation of partial sums
	pBoss->m_dResult = 0;
	for(i=0; i<pBoss->m_dwCalcThreads; i++)
		pBoss->m_dResult += pBoss->m_pSums[i];
	
	d4d(_T("Result is %f; master thread quit, bye..."), pBoss->m_dResult);

	return 0;
}

int CBoxBoss::QuitSlaves(bool bQuitRoguish)
{
	return 0;
}

int CBoxBoss::NewTask(double x, uint32_t dwNumThreads, uint32_t dwMemberCount)
{
	if(m_pSums!=NULL)
	{
		d4d(_T("Not clean, JIT cleanup will be performed..."));
		Cleanup();
	}
	m_dXtoCalc = x, 
	m_dResult = 0.0;
	m_dwThreadCount = 0;
	m_dwCalcThreads = dwNumThreads;
	m_dwSeriesMemberCount = dwMemberCount;

	m_pSums = new double[dwNumThreads];
	m_pThreadParams = new THREADPARAM[dwNumThreads];

	d4d(_T("Task force to calc Mercator series for x = %f was prepared."), m_dXtoCalc);
	d4d(_T("Count of series members is %d."), dwMemberCount);

	return 0;
}

int CBoxBoss::StartCalc()
{
	int hRes = 0;
	
	for(unsigned int i=0; i<this->m_dwCalcThreads;i++)
	{
		m_pThreadParams[i].dwIndex=i;
		m_pThreadParams[i].pBoss=this;
        this->m_vecWorkerThreads.push_back(std::thread(CalcThreadProc, &(m_pThreadParams[i])));
	}
	d4d(_T("%d worker threads were created."), m_dwCalcThreads);
	if(m_pMasterThread != NULL) {
		m_dwThreadCount = 0;
		m_pMasterThread->join();
		delete m_pMasterThread;
	}
	this->m_pMasterThread = new std::thread(MasterThreadProc, this);
	d4d(_T("Master thread (handle %d) was created successfully."), this->m_pMasterThread->get_id());

	return hRes;
}

int CBoxBoss::Cleanup()
{
	if(m_pSums != NULL)
	{
		delete []m_pSums;
		m_pSums = NULL;
	}
	if(m_pThreadParams != NULL)
	{
		delete []m_pThreadParams;
		m_pThreadParams = NULL;
	}

	return 0;
}

uint32_t CBoxBoss::QueryProgress(uint32_t dwWaitMS)
{
	uint32_t dwWait;
	
	d4d(_T("Value of the thread count field is %d."), m_dwThreadCount);

	return this->m_dwThreadCount;
}

void CBoxBoss::CloseThreadHandles()
{
}

int CBoxBoss::SetNotify(bool bNotify)
{
	this->m_bNeedNotify = bNotify;
	d4d(_T("Notify bad behavior flag is %d"),m_bNeedNotify);
	return 0;
}

bool CBoxBoss::CloseCSHandle()
{
    return true;
}
