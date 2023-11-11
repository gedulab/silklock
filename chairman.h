#ifndef __CHAIR_MAN_H__

class ChairMan
{
protected:
    int m_nNoticeCounter;    
public:
    ChairMan();
    void Notify(int worker_id, int notice);    
};

#endif // __CHAIR_MAN_H__
