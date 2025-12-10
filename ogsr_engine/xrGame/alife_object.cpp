////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_object.cpp
//	Created 	: 27.10.2005
//  Modified 	: 27.10.2005
//	Author		: Dmitriy Iassenev
//	Description : ALife object class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "xrServer_Objects_ALife.h"
#include "alife_simulator.h"
#include "xrServer_Objects_ALife_Items.h"

void CSE_ALifeObject::spawn_supplies() { spawn_supplies(*m_ini_string); }

void CSE_ALifeObject::spawn_supplies(LPCSTR ini_string)
{
    if (!ini_string)
        return;

    if (!xr_strlen(ini_string))
        return;

    //Msg("alifeobject");
    //Msg("! Full supplies string: [%s]", ini_string);

    IReader r((void*)(ini_string), strlen(ini_string));
    CInifile ini(&r, FS.get_path("$game_config$")->m_Path);


    if (ini.section_exist("spawn"))
    {
        LPCSTR N, V;
        float p;
        u32 group_total = 0;
        u32 group_r = 0;
        bool group_chosen = false;

        for (u32 k = 0, j; ini.r_line("spawn", k, &N, &V); k++)
        {
            if (!N || xr_strlen(N) == 0) continue;

            //Msg("* Parsing line: [%s]", N);
            if (strchr(N, ':'))
            {
                if (0 == strncmp(N, "group", 5))
                {
                    const char* colon = strchr(N, ':');
                    if (colon)
                    {
                        //Msg("! New group: %s", N);
                        group_total = (u32)atol(colon + 1);
                        group_r = ::Random.randI(group_total);
                        group_chosen = false;
                        continue;
                    }
                }

                if (group_total > 0)
                {
                    char weight_str[64], items_str[256];
                    _GetItem(N, 0, weight_str, ':', "1", true);
                    _GetItem(N, 1, items_str, ':', "", true);

                    if (!group_chosen)
                    {
                        u32 w = (u32)atol(weight_str);

                        if (group_r < w)
                        {
                            int cnt = _GetItemCount(items_str, ',');
                            for (int i = 0; i < cnt; ++i)
                            {
                                char item[128];
                                _GetItem(items_str, i, item, ',', "", true);
                                if (item[0])
                                {
                                    CSE_Abstract* E = alife().spawn_item(item, o_Position, m_tNodeID, m_tGraphID, ID);
                                    CSE_ALifeItemWeapon* W = smart_cast<CSE_ALifeItemWeapon*>(E);
                                    if (W) W->m_fCondition = 0.1f + ::Random.randF(0.2f);
                                }
                            }
                            group_chosen = true;
                        }
                        else
                        {
                            group_r -= w;
                        }
                    }
                    continue;
                }
            }
            VERIFY(xr_strlen(N));

            float f_cond = 1.0f;
            bool bScope = false;
            bool bSilencer = false;
            bool bLauncher = false;

            j = 1;
            p = 1.f;

            if (V && xr_strlen(V))
            {
                string64 buf;
                j = atoi(_GetItem(V, 0, buf));
                if (!j)
                    j = 1;

                bScope = (NULL != strstr(V, "scope"));
                bSilencer = (NULL != strstr(V, "silencer"));
                bLauncher = (NULL != strstr(V, "launcher"));
                // probability
                if (NULL != strstr(V, "prob="))
                    p = (float)atof(strstr(V, "prob=") + 5);
                if (fis_zero(p))
                    p = 1.0f;
                if (NULL != strstr(V, "cond="))
                    f_cond = (float)atof(strstr(V, "cond=") + 5);
            }
            for (u32 i = 0; i < j; ++i)
            {
                if (::Random.randF(1.f) < p)
                {
                    CSE_Abstract* E = alife().spawn_item(N, o_Position, m_tNodeID, m_tGraphID, ID);
                    //подсоединить аддоны к оружию, если включены соответствующие флажки
                    CSE_ALifeItemWeapon* W = smart_cast<CSE_ALifeItemWeapon*>(E);
                    if (W)
                    {
                        if (f_cond == 1.0f)
                        {
                            f_cond = 0.1f + ::Random.randF(0.2f);;
                        }
                        if (W->m_scope_status == CSE_ALifeItemWeapon::eAddonAttachable)
                            W->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonScope, bScope);
                        if (W->m_silencer_status == CSE_ALifeItemWeapon::eAddonAttachable)
                            W->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonSilencer, bSilencer);
                        if (W->m_grenade_launcher_status == CSE_ALifeItemWeapon::eAddonAttachable)
                            W->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher, bLauncher);
                    }
                    CSE_ALifeInventoryItem* IItem = smart_cast<CSE_ALifeInventoryItem*>(E);
                    if (IItem)
                    {
                        IItem->m_fCondition = f_cond;
                    }
                }
            }
        }
    }
}

bool CSE_ALifeObject::keep_saved_data_anyway() const { return (false); }
