#include "stdafx.h"
#include "inventory_item.h"
#include "ai_space.h"
#include "alife_simulator.h"

void CInventoryItem::net_Spawn_install_upgrades(CSE_Abstract* DC)
{
    if (!ai().get_alife())
    {
        return;
    }

    CSE_ALifeInventoryItem* pSE_InventoryItem = smart_cast<CSE_ALifeInventoryItem*>(DC);
    if (!pSE_InventoryItem)
    {
        return;
    }

   /* Upgrades_type saved_upgrades = pSE_InventoryItem->m_upgrades;

    m_upgrades.clear();*/

    //ai().alife().inventory_upgrade_manager().init_install(*this); // from pSettings

    //for (const auto& upgrade : saved_upgrades)
    //{
    //    ai().alife().inventory_upgrade_manager().upgrade_install(*this, *upgrade, true);
    //}
};