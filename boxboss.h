#ifndef __BOX_BOSS_H__
#define __BOX_BOSS_H__
#include <stdint.h>
#include <stdbool.h>
#include <thread>         // std::thread
#include <mutex>          // std::mutex
#include <vector>
#include "chairman.h"

#define BUGBUG
class CBoxBoss;

typedef struct _ADVSETTING
{
	uint32_t dwWaitMS;
	bool  bQuitAll;
}ADVSETTING,*PADVSETTING;

typedef struct _THREADPARAM
{
	uint32_t dwIndex;
	CBoxBoss * pBoss;
}THREADPARAM,*PTHREADPARAM;

#define BOX_MAX_DOORBELLS 6

class CBoxBoss 
{
public:
	int QuitSlaves(bool bQuitRoguish);
	int Setup(ChairMan * pChairMan);
	CBoxBoss();
	virtual ~CBoxBoss();
	bool GetSettings(ADVSETTING * pSettings);
public:
	bool CloseCSHandle();
	int SetNotify(bool bNotify);
	bool GetNotify(){return m_bNeedNotify;}
	uint32_t QueryProgress(uint32_t dwWaitMS);
	int Cleanup();
	int StartCalc();
	int NewTask(double x,uint32_t dwNumThreads, uint32_t dwMemberCount);
	void Notify(int thread_id, int notice);
	void CloseThreadHandles();
    std::vector<std::thread> m_vecWorkerThreads;
	//pthread_t *m_pThreadHandles;
	// pthread_t m_hMasterThread;
    std::thread* m_pMasterThread;
	//CRITICAL_SECTION m_CountCS;
    std::mutex m_CountLock;

#ifdef BUGBUG
	uint32_t m_dwThreadCount; // counter of threads who are done
	uint32_t m_dwCalcThreads; // total number of the calculating threads
#else
	volatile uint32_t m_dwThreadCount; // counter of threads who are done
	volatile uint32_t m_dwCalcThreads; // total number of the calculating threads
#endif
	uint32_t m_dwSeriesMemberCount;
	PTHREADPARAM m_pThreadParams;
	double *m_pSums;
	double m_dXtoCalc; 
	double m_dResult;
	bool  m_bQuitRoguish; // don't let other threads know the quit news
	bool  m_bNeedNotify; // need notify chairman
	uint32_t m_nDoorBells[BOX_MAX_DOORBELLS];
	ChairMan * m_pChairMan;
};

#endif // __BOX_BOSS_H__