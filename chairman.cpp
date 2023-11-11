#include "chairman.h"
void d4d(const char * format, ...);

ChairMan::ChairMan()
{
    this->m_nNoticeCounter = 0;
}
void ChairMan::Notify(int worker_id, int notice)
{
    d4d("chairman got notice %d from worker %d", notice, worker_id);
    this->m_nNoticeCounter++;
};