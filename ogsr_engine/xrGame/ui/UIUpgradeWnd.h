#pragma once
#include "UIWindow.h"

class CInventoryOwner;
struct CUIUpgradeInternal;

class CUIUpgradeWnd : public CUIWindow
{
private:
    typedef CUIWindow inherited;

public:
    CUIUpgradeWnd();
    virtual ~CUIUpgradeWnd();

    virtual void Init();
    virtual void Draw();
    virtual void Show();
    virtual void Hide();
    virtual void SendMessage(CUIWindow* pWnd, s16 msg, void* pData);
    void SwitchToTalk();
    void InitUpgrade(CInventoryOwner* pOur, CInventoryOwner* pOthers);
protected:
    CUIUpgradeInternal* m_uidata;
    CInventoryOwner* m_pInvOwner;
    CInventoryOwner* m_pOthersInvOwner;
};