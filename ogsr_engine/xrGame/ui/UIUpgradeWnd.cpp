#include "stdafx.h"
#include "UIUpgradeWnd.h"

#include "xrUIXmlParser.h"
#include "UIXmlInit.h"
#include "../Entity.h"
#include "../HUDManager.h"
#include "../WeaponAmmo.h"
#include "../Actor.h"
#include "../Trade.h"
#include "../UIGameSP.h"
#include "UIInventoryUtilities.h"
#include "../inventoryowner.h"
#include "../eatable_item.h"
#include "../inventory.h"
#include "../level.h"
#include "../string_table.h"
#include "../character_info.h"
#include "UIMultiTextStatic.h"
#include "UI3tButton.h"
#include "UIItemInfo.h"

#include "UICharacterInfo.h"
#include "UIDragDropListEx.h"
#include "UICellItem.h"
#include "UICellItemFactory.h"
#include "UIPropertiesBox.h"
#include "UIListBoxItem.h"

#include "../game_object_space.h"
#include "../script_callback_ex.h"
#include "../script_game_object.h"
#include "../xr_3da/xr_input.h"
#include <format>
#define UP_XML "trade.xml"
#define UP_CHARACTER_XML "trade_character.xml"
#define UP_ITEM_XML "trade_item.xml"

struct CUIUpgradeInternal
{
    CUIStatic UIStaticTop;
    CUIStatic UIStaticBottom;

    CUIStatic UIOurBagWnd;
    CUIStatic UIOurMoneyStatic;
    CUIStatic UIOthersBagWnd;
    CUIStatic UIOtherMoneyStatic;
    CUIDragDropListEx UIOurBagList;
    CUIDragDropListEx UIOthersBagList;

    CUIStatic UIOurTradeWnd;
    CUIStatic UIOthersTradeWnd;
    CUIMultiTextStatic UIOurPriceCaption;
    CUIMultiTextStatic UIOthersPriceCaption;
    CUIDragDropListEx UIOurTradeList;
    CUIDragDropListEx UIOthersTradeList;

    //кнопки
    CUI3tButton UIPerformTradeButton;
    CUI3tButton UIToTalkButton;

    //информация о персонажах
    CUIStatic UIOurIcon;
    CUIStatic UIOthersIcon;
    CUICharacterInfo UICharacterInfoLeft;
    CUICharacterInfo UICharacterInfoRight;

    //информация о перетаскиваемом предмете
    CUIStatic UIDescWnd;
    CUIItemInfo UIItemInfo;

    SDrawStaticStruct* UIDealMsg;
};

CUIUpgradeWnd::CUIUpgradeWnd()
{
    m_uidata = xr_new<CUIUpgradeInternal>();
    Init();
    Hide();
}

CUIUpgradeWnd::~CUIUpgradeWnd()
{
    xr_delete(m_uidata);
}

void CUIUpgradeWnd::Init()
{
    CUIXml uiXml;
    bool xml_result = uiXml.Init(CONFIG_PATH, UI_PATH, UP_XML);
    R_ASSERT3(xml_result, "xml file not found", UP_XML);
    CUIXmlInit xml_init;

    xml_init.InitWindow(uiXml, "main", 0, this);

    //статические элементы интерфейса
    AttachChild(&m_uidata->UIStaticTop);
    xml_init.InitStatic(uiXml, "top_background", 0, &m_uidata->UIStaticTop);
    AttachChild(&m_uidata->UIStaticBottom);
    xml_init.InitStatic(uiXml, "bottom_background", 0, &m_uidata->UIStaticBottom);

    //иконки с изображение нас и партнера по торговле
    AttachChild(&m_uidata->UIOurIcon);
    xml_init.InitStatic(uiXml, "static_icon", 0, &m_uidata->UIOurIcon);
    AttachChild(&m_uidata->UIOthersIcon);
    xml_init.InitStatic(uiXml, "static_icon", 1, &m_uidata->UIOthersIcon);
    m_uidata->UIOurIcon.AttachChild(&m_uidata->UICharacterInfoLeft);
    m_uidata->UICharacterInfoLeft.Init(0, 0, m_uidata->UIOurIcon.GetWidth(), m_uidata->UIOurIcon.GetHeight(), UP_CHARACTER_XML);
    m_uidata->UIOthersIcon.AttachChild(&m_uidata->UICharacterInfoRight);
    m_uidata->UICharacterInfoRight.Init(0, 0, m_uidata->UIOthersIcon.GetWidth(), m_uidata->UIOthersIcon.GetHeight(), UP_CHARACTER_XML);
    AttachChild(&m_uidata->UIToTalkButton);
    xml_init.Init3tButton(uiXml, "button", 1, &m_uidata->UIToTalkButton);
}


void CUIUpgradeWnd::Draw()
{
    inherited::Draw();
}

void CUIUpgradeWnd::Show()
{
    inherited::Show(true);
    inherited::Enable(true);
}

void CUIUpgradeWnd::Hide()
{
    inherited::Show(false);
    inherited::Enable(false);
}

void CUIUpgradeWnd::InitUpgrade(CInventoryOwner* pOur, CInventoryOwner* pOthers)
{
    VERIFY(pOur);
    VERIFY(pOthers);

    m_pInvOwner = pOur;
    m_pOthersInvOwner = pOthers;

    m_uidata->UICharacterInfoLeft.InitCharacter(m_pInvOwner->object_id());
    m_uidata->UICharacterInfoRight.InitCharacter(m_pOthersInvOwner->object_id());
}

void CUIUpgradeWnd::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
    if (pWnd == &m_uidata->UIToTalkButton && msg == BUTTON_CLICKED)
    {
        SwitchToTalk();
    }

    CUIWindow::SendMessage(pWnd, msg, pData);
}

void CUIUpgradeWnd::SwitchToTalk() { GetMessageTarget()->SendMessage(this, UPGRADE_WND_CLOSED); }