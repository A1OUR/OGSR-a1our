// Weapon.cpp: implementation of the CWeapon class.
//
//////////////////////////////////////////////////////////////////////
#include "stdafx.h"

#include "Weapon.h"
#include "ParticlesObject.h"
#include "entity_alive.h"
#include "player_hud.h"
#include "inventory_item_impl.h"

#include "inventory.h"
#include "xrserver_objects_alife_items.h"

#include "actor.h"
#include "actoreffector.h"
#include "level.h"

#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "../Include/xrRender/Kinematics.h"
#include "ai_object_location.h"
#include "clsid_game.h"
#include "object_broker.h"
#include "../xr_3da/LightAnimLibrary.h"
#include "game_object_space.h"
#include "script_game_object.h"

#include "GamePersistent.h"
#include "../xr_3da/x_ray.h"

#define ROTATION_TIME 0.25f

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CWeapon::CWeapon(LPCSTR name)
{
    SetState(eHidden);
    SetNextState(eHidden);
    m_sub_state = eSubstateReloadBegin;
    m_idle_state = eIdle;
    m_bTriStateReload = false;
    SetDefaults();

    m_Offset.identity();
    m_StrapOffset.identity();

    iAmmoCurrent = -1;
    m_dwAmmoCurrentCalcFrame = 0;

    iAmmoElapsed = -1;
    iMagazineSize = -1;
    m_ammoType = 0;

    eHandDependence = hdNone;

    m_fZoomFactor = Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.f : g_fov;

    m_fZoomRotationFactor = 0.f;

    m_pAmmo = nullptr;

    m_pFlameParticles2 = nullptr;
    m_sFlameParticles2 = nullptr;

    m_fCurrentCartirdgeDisp = 1.f;

    m_strap_bone0 = nullptr;
    m_strap_bone1 = nullptr;
    m_StrapOffset.identity();
    m_strapped_mode = false;
    m_can_be_strapped = false;
    m_ef_main_weapon_type = u32(-1);
    m_ef_weapon_type = u32(-1);
    m_UIScope = nullptr;
    m_set_next_ammoType_on_reload = u32(-1);
}

CWeapon::~CWeapon()
{
    xr_delete(m_UIScope);

    laser_light_render.destroy();
    flashlight_render.destroy();
    flashlight_omni.destroy();
    flashlight_glow.destroy();
}

void CWeapon::Hit(SHit* pHDS) { inherited::Hit(pHDS); }

void CWeapon::UpdateXForm()
{
    if (Device.dwFrame != dwXF_Frame)
    {
        dwXF_Frame = Device.dwFrame;

        // Get access to entity and its visual
        CEntityAlive* E = smart_cast<CEntityAlive*>(H_Parent());

        if (!E)
            return;

        const CInventoryOwner* parent = smart_cast<const CInventoryOwner*>(E);
        if (!parent || parent && parent->use_simplified_visual())
            return;

        if (parent->attached(this))
            return;

        IKinematics* V = smart_cast<IKinematics*>(E->Visual());
        VERIFY(V);

        // Get matrices
        int boneL{BI_NONE}, boneR{BI_NONE}, boneR2{BI_NONE};

        E->g_WeaponBones(boneL, boneR, boneR2);

        if ((HandDependence() == hd1Hand) || (GetState() == eReload) || (!E->g_Alive()))
            boneL = boneR2;

        // KRodin: видимо такое случается иногда у некоторых визуалов нпс. Например если создать нпс с визуалом монстра наверно.
        if (boneL == BI_NONE || boneR == BI_NONE)
            return;

        if (auto pActor = smart_cast<CActor*>(H_Parent()); pActor && pActor->cam_Active() != pActor->cam_FirstEye())
        {
            // https://www.gameru.net/forum/index.php?s=&showtopic=23443&view=findpost&p=1677678
            V->CalculateBones_Invalidate();
            V->CalculateBones(true);
        }
        else
        {
            V->CalculateBones();
        }

        const Fmatrix& mL = V->LL_GetTransform(u16(boneL));
        const Fmatrix& mR = V->LL_GetTransform(u16(boneR));
        // Calculate
        Fmatrix mRes;
        Fvector R, D, N;
        D.sub(mL.c, mR.c);

        if (fis_zero(D.magnitude()))
        {
            mRes.set(E->XFORM());
            mRes.c.set(mR.c);
        }
        else
        {
            D.normalize();
            R.crossproduct(mR.j, D);

            N.crossproduct(D, R);
            N.normalize();

            mRes.set(R, N, D, mR.c);
            mRes.mulA_43(E->XFORM());
        }

        UpdatePosition(mRes);
    }
}

void CWeapon::UpdateFireDependencies_internal()
{
    if (skip_updated_frame == Device.dwFrame || Device.dwFrame != dwFP_Frame)
    {
        dwFP_Frame = Device.dwFrame;

        UpdateXForm();

        if (GetHUDmode())
        {
            HudItemData()->setup_firedeps(m_current_firedeps);
            VERIFY(_valid(m_current_firedeps.m_FireParticlesXForm));
        }
        else
        {
            // 3rd person or no parent
            Fmatrix& parent = XFORM();
            Fvector& fp = vLoadedFirePoint;
            Fvector& fp2 = vLoadedFirePoint2;
            Fvector& sp = vLoadedShellPoint;

            parent.transform_tiny(m_current_firedeps.vLastFP, fp);
            parent.transform_tiny(m_current_firedeps.vLastFP2, fp2);
            parent.transform_tiny(m_current_firedeps.vLastSP, sp);
            parent.transform_tiny(m_current_firedeps.vLastShootPoint, fp);

            m_current_firedeps.vLastFD.set(0.f, 0.f, 1.f);
            parent.transform_dir(m_current_firedeps.vLastFD);

            m_current_firedeps.m_FireParticlesXForm.set(parent);
            VERIFY(_valid(m_current_firedeps.m_FireParticlesXForm));
        }
    }
}

void CWeapon::ForceUpdateFireParticles()
{
    if (!GetHUDmode())
    { // update particlesXFORM real bullet direction

        if (!H_Parent())
            return;

        Fvector p, d;
        smart_cast<CEntity*>(H_Parent())->g_fireParams(this, p, d);

        Fmatrix _pxf;
        _pxf.k = d;
        _pxf.i.crossproduct(Fvector().set(0.0f, 1.0f, 0.0f), _pxf.k);
        _pxf.j.crossproduct(_pxf.k, _pxf.i);
        _pxf.c = XFORM().c;

        m_current_firedeps.m_FireParticlesXForm.set(_pxf);
    }
}

constexpr const char* wpn_scope_def_bone = "wpn_scope";
constexpr const char* wpn_silencer_def_bone = "wpn_silencer";
constexpr const char* wpn_launcher_def_bone_shoc = "wpn_launcher";
constexpr const char* wpn_launcher_def_bone_cop = "wpn_grenade_launcher";

void CWeapon::Load(LPCSTR section)
{
    inherited::Load(section);
    CShootingObject::Load(section);

    if (pSettings->line_exist(section, "flame_particles_2"))
        m_sFlameParticles2 = pSettings->r_string(section, "flame_particles_2");

    if (!m_bForcedParticlesHudMode)
        m_bParticlesHudMode = !!pSettings->line_exist(hud_sect, "item_visual");

#ifdef DEBUG
    {
        Fvector pos, ypr;
        pos = pSettings->r_fvector3(section, "position");
        ypr = pSettings->r_fvector3(section, "orientation");
        ypr.mul(PI / 180.f);

        m_Offset.setHPB(ypr.x, ypr.y, ypr.z);
        m_Offset.translate_over(pos);
    }

    m_StrapOffset = m_Offset;
    if (pSettings->line_exist(section, "strap_position") && pSettings->line_exist(section, "strap_orientation"))
    {
        Fvector pos, ypr;
        pos = pSettings->r_fvector3(section, "strap_position");
        ypr = pSettings->r_fvector3(section, "strap_orientation");
        ypr.mul(PI / 180.f);

        m_StrapOffset.setHPB(ypr.x, ypr.y, ypr.z);
        m_StrapOffset.translate_over(pos);
    }
#endif

    // load ammo classes
    m_ammoTypes.clear();
    LPCSTR S = pSettings->r_string(section, "ammo_class");
    if (S && S[0])
    {
        string128 _ammoItem;
        int count = _GetItemCount(S);
        for (int it = 0; it < count; ++it)
        {
            _GetItem(S, it, _ammoItem);
            m_ammoTypes.push_back(_ammoItem);
        }
    }

    iAmmoElapsed = pSettings->r_s32(section, "ammo_elapsed");
    iMagazineSize = pSettings->r_s32(section, "ammo_mag_size");

    ////////////////////////////////////////////////////
    // дисперсия стрельбы

    //подбрасывание камеры во время отдачи
    camMaxAngle = pSettings->r_float(section, "cam_max_angle");
    camMaxAngle = deg2rad(camMaxAngle);
    camRelaxSpeed = pSettings->r_float(section, "cam_relax_speed");
    camRelaxSpeed = deg2rad(camRelaxSpeed);
    if (pSettings->line_exist(section, "cam_relax_speed_ai"))
    {
        camRelaxSpeed_AI = pSettings->r_float(section, "cam_relax_speed_ai");
        camRelaxSpeed_AI = deg2rad(camRelaxSpeed_AI);
    }
    else
    {
        camRelaxSpeed_AI = camRelaxSpeed;
    }

    //	camDispersion		= pSettings->r_float		(section,"cam_dispersion"	);
    //	camDispersion		= deg2rad					(camDispersion);

    camMaxAngleHorz = pSettings->r_float(section, "cam_max_angle_horz");
    camMaxAngleHorz = deg2rad(camMaxAngleHorz);
    camStepAngleHorz = pSettings->r_float(section, "cam_step_angle_horz");
    camStepAngleHorz = deg2rad(camStepAngleHorz);
    camDispertionFrac = READ_IF_EXISTS(pSettings, r_float, section, "cam_dispertion_frac", 0.7f);
    //  [8/2/2005]
    // m_fParentDispersionModifier = READ_IF_EXISTS(pSettings, r_float, section, "parent_dispersion_modifier",1.0f);
    m_fPDM_disp_base = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_base", 1.0f);
    m_fPDM_disp_vel_factor = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_vel_factor", 1.0f);
    m_fPDM_disp_accel_factor = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_accel_factor", 1.0f);
    m_fPDM_disp_crouch = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_crouch", 1.0f);
    m_fPDM_disp_crouch_no_acc = READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_crouch_no_acc", 1.0f);
    //  [8/2/2005]

    fireDispersionConditionFactor = pSettings->r_float(section, "fire_dispersion_condition_factor");
    misfireProbability = pSettings->r_float(section, "misfire_probability");
    misfireConditionK = READ_IF_EXISTS(pSettings, r_float, section, "misfire_condition_k", 1.0f);
    conditionDecreasePerShot = pSettings->r_float(section, "condition_shot_dec");
    conditionDecreasePerShotOnHit = READ_IF_EXISTS(pSettings, r_float, section, "condition_shot_dec_on_hit", 0.f);
    conditionDecreasePerShotSilencer = READ_IF_EXISTS(pSettings, r_float, section, "condition_shot_dec_silencer", conditionDecreasePerShot);

    vLoadedFirePoint = pSettings->r_fvector3(section, "fire_point");

    if (pSettings->line_exist(section, "fire_point2"))
        vLoadedFirePoint2 = pSettings->r_fvector3(section, "fire_point2");
    else
        vLoadedFirePoint2 = vLoadedFirePoint;

    // hands
    eHandDependence = EHandDependence(pSettings->r_s32(section, "hand_dependence"));
    m_bIsSingleHanded = true;
    if (pSettings->line_exist(section, "single_handed"))
        m_bIsSingleHanded = !!pSettings->r_bool(section, "single_handed");
    //
    m_fMinRadius = pSettings->r_float(section, "min_radius");
    m_fMaxRadius = pSettings->r_float(section, "max_radius");

    // информация о возможных апгрейдах и их визуализации в инвентаре
    m_eScopeStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "scope_status");
    m_eSilencerStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "silencer_status");
    m_eGrenadeLauncherStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section, "grenade_launcher_status");

    if (m_eSilencerStatus == ALife::eAddonPermanent)
        m_bLightShotEnabled = false;

    m_bZoomEnabled = !!pSettings->r_bool(section, "zoom_enabled");
    m_bUseScopeZoom = !!READ_IF_EXISTS(pSettings, r_bool, section, "use_scope_zoom", false);
    m_bUseScopeGrenadeZoom = !!READ_IF_EXISTS(pSettings, r_bool, section, "use_scope_grenade_zoom", false);
    m_bScopeShowIndicators = !!READ_IF_EXISTS(pSettings, r_bool, section, "scope_show_indicators", true);
    m_bIgnoreScopeTexture = !!READ_IF_EXISTS(pSettings, r_bool, section, "ignore_scope_texture", false);

    m_fZoomRotateTime = READ_IF_EXISTS(pSettings, r_float, hud_sect, "zoom_rotate_time", ROTATION_TIME);

    m_bScopeDynamicZoom = false;
    m_fScopeZoomFactor = 0;
    m_fRTZoomFactor = 0;

    m_fZoomFactor = CurrentZoomFactor();

    m_allScopeNames.clear();
    m_highlightAddons.clear();
    if (m_eScopeStatus == ALife::eAddonAttachable)
    {
        m_sScopeName = pSettings->r_string(section, "scope_name");
        m_iScopeX = pSettings->r_s32(section, "scope_x");
        m_iScopeY = pSettings->r_s32(section, "scope_y");

        m_allScopeNames.push_back(m_sScopeName);
        if (pSettings->line_exist(section, "scope_names"))
        {
            LPCSTR S = pSettings->r_string(section, "scope_names");
            if (S && S[0])
            {
                string128 _scopeItem;
                int count = _GetItemCount(S);
                for (int it = 0; it < count; ++it)
                {
                    _GetItem(S, it, _scopeItem);
                    m_allScopeNames.push_back(_scopeItem);
                    m_highlightAddons.push_back(_scopeItem);
                }
            }
        }
    }

    if (m_eSilencerStatus == ALife::eAddonAttachable)
    {
        m_sSilencerName = pSettings->r_string(section, "silencer_name");
        m_iSilencerX = pSettings->r_s32(section, "silencer_x");
        m_iSilencerY = pSettings->r_s32(section, "silencer_y");
    }

    if (m_eGrenadeLauncherStatus == ALife::eAddonAttachable)
    {
        m_sGrenadeLauncherName = pSettings->r_string(section, "grenade_launcher_name");
        m_iGrenadeLauncherX = pSettings->r_s32(section, "grenade_launcher_x");
        m_iGrenadeLauncherY = pSettings->r_s32(section, "grenade_launcher_y");
    }

    // Кости мировой модели оружия
    if (pSettings->line_exist(section, "scope_bone"))
    {
        const char* S = pSettings->r_string(section, "scope_bone");
        if (S && strlen(S))
        {
            const int count = _GetItemCount(S);
            string128 _scope_bone{};
            for (int it = 0; it < count; ++it)
            {
                _GetItem(S, it, _scope_bone);
                m_sWpn_scope_bones.push_back(_scope_bone);
            }
        }
        else
            m_sWpn_scope_bones.push_back(wpn_scope_def_bone);
    }
    else
        m_sWpn_scope_bones.push_back(wpn_scope_def_bone);
    m_sWpn_silencer_bone = READ_IF_EXISTS(pSettings, r_string, section, "silencer_bone", wpn_silencer_def_bone);
    m_sWpn_launcher_bone = READ_IF_EXISTS(pSettings, r_string, section, "launcher_bone", wpn_launcher_def_bone_shoc);
    m_sWpn_laser_bone = READ_IF_EXISTS(pSettings, r_string, section, "laser_ray_bones", "");
    m_sWpn_flashlight_bone = READ_IF_EXISTS(pSettings, r_string, section, "torch_cone_bones", "");

    if (pSettings->line_exist(section, "hidden_bones"))
    {
        const char* S = pSettings->r_string(section, "hidden_bones");
        if (S && strlen(S))
        {
            const int count = _GetItemCount(S);
            string128 _hidden_bone{};
            for (int it = 0; it < count; ++it)
            {
                _GetItem(S, it, _hidden_bone);
                hidden_bones.push_back(_hidden_bone);
            }
        }
    }

    // Кости худовой модели оружия - если не прописаны, используются имена из конфига мировой модели.
    if (pSettings->line_exist(hud_sect, "scope_bone"))
    {
        const char* S = pSettings->r_string(hud_sect, "scope_bone");
        if (S && strlen(S))
        {
            const int count = _GetItemCount(S);
            string128 _scope_bone{};
            for (int it = 0; it < count; ++it)
            {
                _GetItem(S, it, _scope_bone);
                m_sHud_wpn_scope_bones.push_back(_scope_bone);
            }
        }
        else
            m_sHud_wpn_scope_bones = m_sWpn_scope_bones;
    }
    else
        m_sHud_wpn_scope_bones = m_sWpn_scope_bones;
    m_sHud_wpn_silencer_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "silencer_bone", m_sWpn_silencer_bone);
    m_sHud_wpn_launcher_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "launcher_bone", m_sWpn_launcher_bone);
    m_sHud_wpn_laser_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "laser_ray_bones", m_sWpn_laser_bone);
    m_sHud_wpn_flashlight_bone = READ_IF_EXISTS(pSettings, r_string, hud_sect, "torch_cone_bones", m_sWpn_flashlight_bone);

    if (pSettings->line_exist(hud_sect, "hidden_bones"))
    {
        const char* S = pSettings->r_string(hud_sect, "hidden_bones");
        if (S && strlen(S))
        {
            const int count = _GetItemCount(S);
            string128 _hidden_bone{};
            for (int it = 0; it < count; ++it)
            {
                _GetItem(S, it, _hidden_bone);
                hud_hidden_bones.push_back(_hidden_bone);
            }
        }
    }
    else
        hud_hidden_bones = hidden_bones;

    //Можно и из конфига прицела читать и наоборот! Пока так.
    m_fZoomHudFov = 0.0f;
    m_f3dssHudFov = 0.0f;
    m_fScopeInertionFactor = m_fControlInertionFactor;

    InitAddons();

    m_bHideCrosshairInZoom = true;
    if (pSettings->line_exist(hud_sect, "zoom_hide_crosshair"))
        m_bHideCrosshairInZoom = !!pSettings->r_bool(hud_sect, "zoom_hide_crosshair");

    m_bZoomInertionAllow =
        READ_IF_EXISTS(pSettings, r_bool, hud_sect, "allow_zoom_inertion", IS_OGSR_GA ? true : READ_IF_EXISTS(pSettings, r_bool, "features", "default_allow_zoom_inertion", true));
    m_bScopeZoomInertionAllow = READ_IF_EXISTS(pSettings, r_bool, hud_sect, "allow_scope_zoom_inertion",
                                               IS_OGSR_GA ? true : READ_IF_EXISTS(pSettings, r_bool, "features", "default_allow_scope_zoom_inertion", true));

    //////////////////////////////////////////////////////////

    m_bHasTracers = READ_IF_EXISTS(pSettings, r_bool, section, "tracers", true);
    m_u8TracerColorID = READ_IF_EXISTS(pSettings, r_u8, section, "tracers_color_ID", u8(-1));

    string256 temp;
    for (int i = egdNovice; i < egdCount; ++i)
    {
        strconcat(sizeof(temp), temp, "hit_probability_", get_token_name(difficulty_type_token, i));
        m_hit_probability[i] = READ_IF_EXISTS(pSettings, r_float, section, temp, 1.f);
    }

    if (pSettings->line_exist(section, "highlight_addons"))
    {
        LPCSTR S = pSettings->r_string(section, "highlight_addons");
        if (S && S[0])
        {
            string128 _addonItem;
            int count = _GetItemCount(S);
            for (int it = 0; it < count; ++it)
            {
                _GetItem(S, it, _addonItem);
                ASSERT_FMT(pSettings->section_exist(_addonItem), "Section [%s] not found!", _addonItem);
                m_highlightAddons.emplace_back(_addonItem);
            }
        }
    }

    if (!laser_light_render && pSettings->line_exist(section, "laser_light_section"))
    {
        has_laser = true;

        laserdot_attach_bone = READ_IF_EXISTS(pSettings, r_string, section, "laserdot_attach_bone", "");
        laserdot_attach_offset =
            Fvector{READ_IF_EXISTS(pSettings, r_float, section, "laserdot_attach_offset_x", 0.0f), READ_IF_EXISTS(pSettings, r_float, section, "laserdot_attach_offset_y", 0.0f),
                    READ_IF_EXISTS(pSettings, r_float, section, "laserdot_attach_offset_z", 0.0f)};
        laserdot_world_attach_offset = Fvector{READ_IF_EXISTS(pSettings, r_float, section, "laserdot_world_attach_offset_x", 0.0f),
                                               READ_IF_EXISTS(pSettings, r_float, section, "laserdot_world_attach_offset_y", 0.0f),
                                               READ_IF_EXISTS(pSettings, r_float, section, "laserdot_world_attach_offset_z", 0.0f)};

        constexpr bool b_r2 = true;

        const char* m_light_section = pSettings->r_string(section, "laser_light_section");

        laser_lanim = LALib.FindItem(READ_IF_EXISTS(pSettings, r_string, m_light_section, "color_animator", ""));

        laser_light_render = ::Render->light_create();
        laser_light_render->set_type(IRender_Light::SPOT);
        laser_light_render->set_shadow(false);
        laser_light_render->set_moveable(true);

        const Fcolor clr = READ_IF_EXISTS(pSettings, r_fcolor, m_light_section, b_r2 ? "color_r2" : "color", (Fcolor{1.0f, 0.0f, 0.0f, 1.0f}));
        laser_fBrightness = clr.intensity();
        laser_light_render->set_color(clr);
        const float range = READ_IF_EXISTS(pSettings, r_float, m_light_section, b_r2 ? "range_r2" : "range", 100.f);
        laser_light_render->set_range(range);
        laser_light_render->set_cone(deg2rad(READ_IF_EXISTS(pSettings, r_float, m_light_section, "spot_angle", 1.f)));
        laser_light_render->set_texture(READ_IF_EXISTS(pSettings, r_string, m_light_section, "spot_texture", nullptr));
    }

    if (!flashlight_render && pSettings->line_exist(section, "flashlight_section"))
    {
        has_flashlight = true;

        flashlight_attach_bone = pSettings->r_string(section, "torch_light_bone");
        flashlight_attach_offset = Fvector{pSettings->r_float(section, "torch_attach_offset_x"), pSettings->r_float(section, "torch_attach_offset_y"),
                                           pSettings->r_float(section, "torch_attach_offset_z")};
        flashlight_omni_attach_offset = Fvector{pSettings->r_float(section, "torch_omni_attach_offset_x"), pSettings->r_float(section, "torch_omni_attach_offset_y"),
                                                pSettings->r_float(section, "torch_omni_attach_offset_z")};
        flashlight_world_attach_offset = Fvector{pSettings->r_float(section, "torch_world_attach_offset_x"), pSettings->r_float(section, "torch_world_attach_offset_y"),
                                                 pSettings->r_float(section, "torch_world_attach_offset_z")};
        flashlight_omni_world_attach_offset =
            Fvector{pSettings->r_float(section, "torch_omni_world_attach_offset_x"), pSettings->r_float(section, "torch_omni_world_attach_offset_y"),
                    pSettings->r_float(section, "torch_omni_world_attach_offset_z")};

        constexpr bool b_r2 = true;

        const char* m_light_section = pSettings->r_string(section, "flashlight_section");

        flashlight_lanim = LALib.FindItem(READ_IF_EXISTS(pSettings, r_string, m_light_section, "color_animator", ""));

        flashlight_render = ::Render->light_create();
        flashlight_render->set_type(IRender_Light::SPOT);
        flashlight_render->set_shadow(ParentIsActor());
        flashlight_render->set_moveable(true);

        const Fcolor clr = READ_IF_EXISTS(pSettings, r_fcolor, m_light_section, b_r2 ? "color_r2" : "color", (Fcolor{0.6f, 0.55f, 0.55f, 1.0f}));
        flashlight_fBrightness = clr.intensity();
        flashlight_render->set_color(clr);
        const float range = READ_IF_EXISTS(pSettings, r_float, m_light_section, b_r2 ? "range_r2" : "range", 50.f);
        flashlight_render->set_range(range);
        flashlight_render->set_cone(deg2rad(READ_IF_EXISTS(pSettings, r_float, m_light_section, "spot_angle", 60.f)));
        flashlight_render->set_texture(READ_IF_EXISTS(pSettings, r_string, m_light_section, "spot_texture", nullptr));

        flashlight_omni = ::Render->light_create();
        flashlight_omni->set_type(
            (IRender_Light::LT)(READ_IF_EXISTS(pSettings, r_u8, m_light_section, "omni_type",
                                               2))); // KRodin: вообще omni это обычно поинт, но поинт светит во все стороны от себя, поэтому тут спот используется по умолчанию.
        flashlight_omni->set_shadow(false);
        flashlight_omni->set_moveable(true);

        const Fcolor oclr = READ_IF_EXISTS(pSettings, r_fcolor, m_light_section, b_r2 ? "omni_color_r2" : "omni_color", (Fcolor{1.0f, 1.0f, 1.0f, 0.0f}));
        flashlight_omni->set_color(oclr);
        const float orange = READ_IF_EXISTS(pSettings, r_float, m_light_section, b_r2 ? "omni_range_r2" : "omni_range", 0.25f);
        flashlight_omni->set_range(orange);

        flashlight_glow = ::Render->glow_create();
        flashlight_glow->set_texture(READ_IF_EXISTS(pSettings, r_string, m_light_section, "glow_texture", "glow\\glow_torch_r2"));
        flashlight_glow->set_color(clr);
        flashlight_glow->set_radius(READ_IF_EXISTS(pSettings, r_float, m_light_section, "glow_radius", 0.3f));
    }

    dof_transition_time = READ_IF_EXISTS(pSettings, r_float, section, "dof_transition_time", 0.6f);
    dof_params_zoom = (READ_IF_EXISTS(pSettings, r_fvector4, section, "dof_zoom_params", (Fvector4{0, 0, 0, 1.6}))); //(Fvector4{0.1, 0.4, 0, 1.6})
    dof_params_reload = (READ_IF_EXISTS(pSettings, r_fvector4, section, "dof_reload_params", (Fvector4{0, 0, 1, 0})));

    dont_interrupt_shot_anm = READ_IF_EXISTS(pSettings, r_bool, section, "dont_interrupt_shot_anm", false);
    is_gunslinger_weapon = READ_IF_EXISTS(pSettings, r_bool, section, "is_gunslinger_weapon", false);

    if (pSettings->line_exist(section, "bullet_textures_in_model"))
    {
        const char* str = pSettings->r_string(section, "bullet_textures_in_model");
        for (int i{}, count = _GetItemCount(str); i < count;)
        {
            xr_string bullet_tex;
            _GetItem(str, i++, bullet_tex);
            bullet_textures_in_model.emplace_back(std::move(bullet_tex));
        }
    }
    if (pSettings->line_exist(section, "bullet_textures_for_ammos"))
    {
        const char* str = pSettings->r_string(section, "bullet_textures_for_ammos");
        for (int i{}, count = _GetItemCount(str); i < count;)
        {
            xr_string ammo_section, bullet_tex;
            _GetItem(str, i++, ammo_section);
            ASSERT_FMT(i < count, "Incorrect [bullet_textures_for_ammos] in section [%s]", section);
            _GetItem(str, i++, bullet_tex);
            bullet_textures_for_ammos.emplace(std::move(ammo_section), std::move(bullet_tex));
        }
    }
}

void CWeapon::LoadFireParams(LPCSTR section, LPCSTR prefix)
{
    camDispersion = pSettings->r_float(section, "cam_dispersion");
    camDispersion = deg2rad(camDispersion);

    if (pSettings->line_exist(section, "cam_dispersion_inc"))
    {
        camDispersionInc = pSettings->r_float(section, "cam_dispersion_inc");
        camDispersionInc = deg2rad(camDispersionInc);
    }
    else
        camDispersionInc = 0;

    CShootingObject::LoadFireParams(section, prefix);
}

BOOL CWeapon::net_Spawn(CSE_Abstract* DC)
{
    BOOL bResult = inherited::net_Spawn(DC);

    auto E = smart_cast<CSE_ALifeItemWeapon*>(DC);

    // iAmmoCurrent					= E->a_current;
    iAmmoElapsed = E->a_elapsed;

    m_flagsAddOnState = E->m_addon_flags.get();
    m_ammoType = E->ammo_type;

    SetState(E->wpn_state);
    SetNextState(E->wpn_state);

    if (m_ammoType >= m_ammoTypes.size())
    {
        Msg("! [%s]: %s: wrong m_ammoType[%u/%u]", __FUNCTION__, cName().c_str(), m_ammoType, m_ammoTypes.size() - 1);
        m_ammoType = 0;
        auto se_obj = alife_object();
        if (se_obj)
        {
            auto W = smart_cast<CSE_ALifeItemWeapon*>(se_obj);
            W->ammo_type = m_ammoType;
        }
    }

    m_DefaultCartridge.Load(*m_ammoTypes[m_ammoType], u8(m_ammoType));
    if (iAmmoElapsed)
    {
        // нож автоматически заряжается двумя патронами, хотя
        // размер магазина у него 0. Что бы зря не ругаться, проверим
        // что в конфиге размер магазина не нулевой.
        if (iMagazineSize && iAmmoElapsed > (iMagazineSize + 1))
        {
            Msg("! [%s]: %s: wrong iAmmoElapsed[%u/%u]", __FUNCTION__, cName().c_str(), iAmmoElapsed, iMagazineSize);
            iAmmoElapsed = iMagazineSize;
            auto se_obj = alife_object();
            if (se_obj)
            {
                auto W = smart_cast<CSE_ALifeItemWeapon*>(se_obj);
                W->a_elapsed = iAmmoElapsed;
            }
        }
        m_fCurrentCartirdgeDisp = m_DefaultCartridge.m_kDisp;
        for (int i = 0; i < iAmmoElapsed; ++i)
            m_magazine.push_back(m_DefaultCartridge);
    }

    UpdateAddonsVisibility();
    InitAddons();

    VERIFY((u32)iAmmoElapsed == m_magazine.size());

    if (m_bLightShotEnabled)
        Light_Create();

    return bResult;
}

void CWeapon::net_Destroy()
{
    inherited::net_Destroy();

    //удалить объекты партиклов
    StopFlameParticles();
    StopFlameParticles2();
    StopLight();
    Light_Destroy();

    m_magazine.clear();
    m_magazine.shrink_to_fit();
}

BOOL CWeapon::IsUpdating()
{
    bool bIsActiveItem = m_pCurrentInventory && m_pCurrentInventory->ActiveItem() == this;
    return bIsActiveItem || bWorking || IsPending() || getVisible();
}

void CWeapon::net_Export(CSE_Abstract* E)
{
    inherited::net_Export(E);

    CSE_ALifeInventoryItem* itm = smart_cast<CSE_ALifeInventoryItem*>(E);
    itm->m_fCondition = m_fCondition;

    CSE_ALifeItemWeapon* wpn = smart_cast<CSE_ALifeItemWeapon*>(E);
    wpn->wpn_flags = IsUpdating() ? 1 : 0;
    wpn->a_elapsed = u16(iAmmoElapsed);
    wpn->m_addon_flags.flags = m_flagsAddOnState;
    wpn->ammo_type = (u8)m_ammoType;
    wpn->wpn_state = (u8)GetState();
    wpn->m_bZoom = (u8)m_bZoomMode;
}

void CWeapon::save(NET_Packet& output_packet)
{
    inherited::save(output_packet);
    save_data(iAmmoElapsed, output_packet);
    save_data(m_flagsAddOnState, output_packet);
    save_data(m_ammoType, output_packet);
    save_data(m_bZoomMode, output_packet);
}

void CWeapon::load(IReader& input_packet)
{
    inherited::load(input_packet);
    load_data(iAmmoElapsed, input_packet);
    load_data(m_flagsAddOnState, input_packet);
    UpdateAddonsVisibility();
    load_data(m_ammoType, input_packet);
    load_data(m_bZoomMode, input_packet);

    if (m_bZoomMode)
        OnZoomIn();
    else
        OnZoomOut();
}

void CWeapon::OnEvent(NET_Packet& P, u16 type)
{
    switch (type)
    {
    case GE_WPN_STATE_CHANGE: {
        u8 state;
        P.r_u8(state);
        P.r_u8(m_sub_state);
        u8 NextAmmo = P.r_u8();
        if (NextAmmo == u8(-1))
            m_set_next_ammoType_on_reload = u32(-1);
        else
            m_set_next_ammoType_on_reload = u8(NextAmmo);

        OnStateSwitch(u32(state), GetState());
    }
    break;
    default: {
        inherited::OnEvent(P, type);
    }
    break;
    }
};

void CWeapon::shedule_Update(u32 dT)
{
    // Inherited
    inherited::shedule_Update(dT);
}

void CWeapon::OnH_B_Independent(bool just_before_destroy)
{
    RemoveShotEffector();

    inherited::OnH_B_Independent(just_before_destroy);

    //завершить принудительно все процессы что шли
    FireEnd();
    SetPending(FALSE);
    SwitchState(eIdle);

    m_strapped_mode = false;
    OnZoomOut();
    m_fZoomRotationFactor = 0.f;
    UpdateXForm();
}

void CWeapon::OnH_A_Independent()
{
    inherited::OnH_A_Independent();
    Light_Destroy();
};

void CWeapon::OnH_A_Chield()
{
    inherited::OnH_A_Chield();

    UpdateAddonsVisibility();
};

void CWeapon::OnActiveItem()
{
    inherited::OnActiveItem();
    //если мы занружаемся и оружие было в руках
    SetState(eIdle);
    SetNextState(eIdle);
}

void CWeapon::OnHiddenItem()
{
    inherited::OnHiddenItem();
    SetState(eHidden);
    SetNextState(eHidden);
    m_set_next_ammoType_on_reload = u32(-1);
}

bool CWeapon::NeedBlendAnm()
{
    /*if (GetState() == eIdle && Actor()->is_safemode())
        return true;*/

    /*if (IsZoomed() && psDeviceFlags2.test(rsAimSway))
        return true;*/

    return inherited::NeedBlendAnm();
}

void CWeapon::OnH_B_Chield()
{
    inherited::OnH_B_Chield();

    OnZoomOut();
    m_set_next_ammoType_on_reload = u32(-1);
}

void CWeapon::OnBeforeDrop()
{
    if (auto io = smart_cast<CActor*>(H_Parent()); io && this == io->inventory().ActiveItem())
        shader_exports.set_dof_params(0.f, 0.f, 0.f, 0.f);

    inherited::OnBeforeDrop();
}

u8 CWeapon::idle_state()
{
    auto* actor = smart_cast<CActor*>(H_Parent());

    if (actor)
    {
        u32 st = actor->get_state();
        if (st & mcSprint)
            return eSubstateIdleSprint;
        else if (st & mcAnyAction && !(st & mcJump) && !(st & mcFall))
            return eSubstateIdleMoving;
    }

    return eIdle;
}

float CWeapon::dof_zoom_effect{}, CWeapon::dof_reload_effect{};

void CWeapon::UpdateCL()
{
    inherited::UpdateCL();

    UpdateHUDAddonsVisibility();

    UpdateVisualBullets();

    //подсветка от выстрела
    UpdateLight();

    //нарисовать партиклы
    UpdateFlameParticles();
    UpdateFlameParticles2();

    VERIFY(smart_cast<IKinematics*>(Visual()));

    CInventoryItem* pActorItem{};
    auto pActor = smart_cast<CActor*>(H_Parent());
    if (pActor)
    {
        pActorItem = pActor->inventory().ActiveItem();
    }

    if (GetState() == eIdle)
    {
        auto state = idle_state();
        if (m_idle_state != state)
        {
            m_idle_state = state;
            if (GetNextState() != eMagEmpty && GetNextState() != eReload)
            {
                SwitchState(eIdle);
            }
        }

        if (pActor && pActorItem == this)
        {
            if (psActorFlags.test(AF_DOF_ZOOM))
            {
                if (m_bZoomMode && dof_zoom_effect < 1.f && !UseScopeTexture() && pActor->active_cam() == ACTOR_DEFS::eacFirstEye)
                    UpdateDof(dof_zoom_effect, Is3dssEnabled() ? dof_params_reload : dof_params_zoom, false);
                else if (dof_zoom_effect > 0.f && (!m_bZoomMode || pActor->active_cam() != ACTOR_DEFS::eacFirstEye))
                    UpdateDof(dof_zoom_effect, Is3dssEnabled() ? dof_params_reload : dof_params_zoom, true);
            }

            if (psActorFlags.test(AF_DOF_RELOAD) && dof_reload_effect > 0.f) 
                UpdateDof(dof_reload_effect, dof_params_reload, true);
        }
    }
    else if (GetState() == eReload)
    {
        if (pActor && pActorItem == this && psActorFlags.test(AF_DOF_RELOAD))
        {
            if (dof_reload_effect < 1.f && pActor->active_cam() == ACTOR_DEFS::eacFirstEye)
                UpdateDof(dof_reload_effect, dof_params_reload, false);
            else if (dof_reload_effect > 0.f && pActor->active_cam() != ACTOR_DEFS::eacFirstEye)
                UpdateDof(dof_reload_effect, dof_params_reload, true);
        }

        m_idle_state = eIdle;
    }
    else
    {
        if (pActor && pActorItem == this)
        {
            if (psActorFlags.test(AF_DOF_RELOAD) && dof_reload_effect > 0.f)
                UpdateDof(dof_reload_effect, dof_params_reload, true);

            if (psActorFlags.test(AF_DOF_ZOOM) && dof_zoom_effect > 0.f && (!m_bZoomMode || pActor->active_cam() != ACTOR_DEFS::eacFirstEye))
                UpdateDof(dof_zoom_effect, Is3dssEnabled() ? dof_params_reload : dof_params_zoom, true);
        }

        m_idle_state = eIdle;
    }

    UpdateLaser();
    UpdateFlashlight();
}

void CWeapon::UpdateDof(float& type, const Fvector4& params_type, const bool desire)
{
    if (desire)
        type -= Device.fTimeDelta / dof_transition_time;
    else
        type += Device.fTimeDelta / dof_transition_time;

    shader_exports.set_dof_params(params_type.x * type, params_type.y * type, params_type.z * type, params_type.w * type);
    clamp(type, 0.f, 1.f);
}

void CWeapon::UpdateLaser()
{
    if (laser_light_render)
    {
        auto io = smart_cast<CInventoryOwner*>(H_Parent());
        if (!laser_light_render->get_active() && IsLaserOn() && (!H_Parent() || (io && this == io->inventory().ActiveItem())))
        {
            laser_light_render->set_active(true);
            UpdateAddonsVisibility();
        }
        else if (laser_light_render->get_active() && (!IsLaserOn() || !(!H_Parent() || (io && this == io->inventory().ActiveItem()))))
        {
            laser_light_render->set_active(false);
            UpdateAddonsVisibility();
        }

        if (laser_light_render->get_active())
        {
            laser_pos = get_LastFP();
            Fvector laser_dir = get_LastFD();

            if (GetHUDmode())
            {
                if (laserdot_attach_bone.size())
                {
                    GetBoneOffsetPosDir(laserdot_attach_bone, laser_pos, laser_dir, laserdot_attach_offset);
                    CorrectDirFromWorldToHud(laser_dir);
                }
            }
            else
            {
                XFORM().transform_tiny(laser_pos, laserdot_world_attach_offset);
            }

            Fmatrix laserXForm;
            laserXForm.identity();
            laserXForm.k.set(laser_dir);
            Fvector::generate_orthonormal_basis_normalized(laserXForm.k, laserXForm.j, laserXForm.i);

            laser_light_render->set_position(laser_pos);
            laser_light_render->set_rotation(laserXForm.k, laserXForm.i);

            // calc color animator
            if (laser_lanim)
            {
                int frame;
                const u32 clr = laser_lanim->CalculateBGR(Device.fTimeGlobal, frame);

                Fcolor fclr{(float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f};
                fclr.mul_rgb(laser_fBrightness / 255.f);
                laser_light_render->set_color(fclr);
            }
        }
    }
}

void CWeapon::UpdateFlashlight()
{
    if (flashlight_render)
    {
        auto io = smart_cast<CInventoryOwner*>(H_Parent());
        if (!flashlight_render->get_active() && IsFlashlightOn() && (!H_Parent() || (io && this == io->inventory().ActiveItem())))
        {
            flashlight_render->set_active(true);
            flashlight_omni->set_active(true);
            flashlight_glow->set_active(true);
            UpdateAddonsVisibility();
        }
        else if (flashlight_render->get_active() && (!IsFlashlightOn() || !(!H_Parent() || (io && this == io->inventory().ActiveItem()))))
        {
            flashlight_render->set_active(false);
            flashlight_omni->set_active(false);
            flashlight_glow->set_active(false);
            UpdateAddonsVisibility();
        }

        if (flashlight_render->get_active())
        {
            Fvector flashlight_pos_omni, flashlight_dir, flashlight_dir_omni;

            if (GetHUDmode())
            {
                GetBoneOffsetPosDir(flashlight_attach_bone, flashlight_pos, flashlight_dir, flashlight_attach_offset);
                CorrectDirFromWorldToHud(flashlight_dir);

                GetBoneOffsetPosDir(flashlight_attach_bone, flashlight_pos_omni, flashlight_dir_omni, flashlight_omni_attach_offset);
                CorrectDirFromWorldToHud(flashlight_dir_omni);
            }
            else
            {
                flashlight_dir = get_LastFD();
                XFORM().transform_tiny(flashlight_pos, flashlight_world_attach_offset);

                flashlight_dir_omni = get_LastFD();
                XFORM().transform_tiny(flashlight_pos_omni, flashlight_omni_world_attach_offset);
            }

            Fmatrix flashlightXForm;
            flashlightXForm.identity();
            flashlightXForm.k.set(flashlight_dir);
            Fvector::generate_orthonormal_basis_normalized(flashlightXForm.k, flashlightXForm.j, flashlightXForm.i);
            flashlight_render->set_position(flashlight_pos);
            flashlight_render->set_rotation(flashlightXForm.k, flashlightXForm.i);

            flashlight_glow->set_position(flashlight_pos);
            flashlight_glow->set_direction(flashlightXForm.k);

            Fmatrix flashlightomniXForm;
            flashlightomniXForm.identity();
            flashlightomniXForm.k.set(flashlight_dir_omni);
            Fvector::generate_orthonormal_basis_normalized(flashlightomniXForm.k, flashlightomniXForm.j, flashlightomniXForm.i);
            flashlight_omni->set_position(flashlight_pos_omni);
            flashlight_omni->set_rotation(flashlightomniXForm.k, flashlightomniXForm.i);

            // calc color animator
            if (flashlight_lanim)
            {
                int frame;
                const u32 clr = flashlight_lanim->CalculateBGR(Device.fTimeGlobal, frame);

                Fcolor fclr{(float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f};
                fclr.mul_rgb(flashlight_fBrightness / 255.f);
                flashlight_render->set_color(fclr);
                flashlight_omni->set_color(fclr);
                flashlight_glow->set_color(fclr);
            }
        }
    }
}

void CWeapon::renderable_Render(u32 context_id, IRenderable* root)
{
    UpdateXForm();

    //нарисовать подсветку
    RenderLight();

    inherited::renderable_Render(context_id, root);
}

void CWeapon::render_hud_mode(u32 context_id, IRenderable* root)
{
    RenderLight();

    inherited::render_hud_mode(context_id, root);
}

bool CWeapon::need_renderable() { return !(IsZoomed() && ZoomTexture() && !IsRotatingToZoom()); }

void CWeapon::signal_HideComplete()
{
    if (H_Parent())
        setVisible(FALSE);
    SetPending(FALSE);
}

void CWeapon::SetDefaults()
{
    bWorking2 = false;
    SetPending(FALSE);

    m_flags.set(FUsingCondition, TRUE);
    m_flagsAddOnState = 0;
    m_bZoomMode = false;
}

void CWeapon::UpdatePosition(const Fmatrix& trans)
{
    Position().set(trans.c);
    XFORM().mul(trans, m_strapped_mode ? m_StrapOffset : m_Offset);
    VERIFY(!fis_zero(DET(renderable.xform)));
}

bool CWeapon::Action(s32 cmd, u32 flags)
{
    if (inherited::Action(cmd, flags))
        return true;

    switch (cmd)
    {
    case kWPN_FIRE: {
        //если оружие чем-то занято, то ничего не делать
        {
            if (flags & CMD_START)
            {
                if (IsPending())
                    return false;
                FireStart();
            }
            else
                FireEnd();
        };
    }
        return true;
    case kWPN_NEXT: {
        if (IsPending())
        {
            return false;
        }

        if (psActorFlags.test(AF_LOCK_RELOAD) && ParentIsActor() && g_actor->get_state() & mcSprint)
            return true;

        if (flags & CMD_START)
        {
            u32 l_newType = m_ammoType;
            bool b1, b2;
            do
            {
                l_newType = (l_newType + 1) % m_ammoTypes.size();
                b1 = l_newType != m_ammoType;
                b2 = unlimited_ammo() ? false : (!m_pCurrentInventory->GetAmmo(*m_ammoTypes[l_newType], ParentIsActor()));
            } while (b1 && b2);

            if (l_newType != m_ammoType)
            {
                m_set_next_ammoType_on_reload = l_newType;

                Reload();
            }
        }
    }
        return true;

    case kWPN_ZOOM: {
        const u32 state = GetState();
        const bool bPending = IsPending();
        if (IsZoomEnabled() && (state == eFire || state == eFire2 || state == eMagEmpty || state == eIdle || !bPending))
        {
            if (flags & CMD_START)
            {
                if (psActorFlags.is(AF_WPN_AIM_TOGGLE) && IsZoomed())
                {
                    OnZoomOut();
                    if (!bPending)
                        SwitchState(eIdle);
                }
                else
                {
                    OnZoomIn();
                    if (!bPending)
                        SwitchState(eIdle);
                }
            }
            else if (IsZoomed() && !psActorFlags.is(AF_WPN_AIM_TOGGLE))
            {
                OnZoomOut();
                if (!bPending)
                    SwitchState(eIdle);
            }
            return true;
        }
        else
            return false;
    }

    case kWPN_ZOOM_INC:
    case kWPN_ZOOM_DEC: {
        if (IsZoomEnabled() && IsZoomed() && m_bScopeDynamicZoom && IsScopeAttached() && (flags & CMD_START))
        {
            // если в режиме ПГ - не будем давать использовать динамический зум
            if (IsGrenadeMode())
                return false;

            ZoomChange(cmd == kWPN_ZOOM_INC);

            return true;
        }
        else
            return false;
    }
    }
    return false;
}

void CWeapon::GetZoomData(const float scope_factor, float& delta, float& min_zoom_factor)
{
    float def_fov = Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.f : g_fov;
    float delta_factor_total = def_fov - scope_factor;
    VERIFY(delta_factor_total > 0);
    min_zoom_factor = def_fov - delta_factor_total * m_fMinZoomK;
    delta = (delta_factor_total * (1 - m_fMinZoomK)) / m_fZoomStepCount;
}

void CWeapon::ZoomChange(bool inc)
{
    bool wasChanged = false;

    if (Is3dssEnabled())
    {
// Simp: переменную кратность в новых прицелах сделал на скриптах, мб в будущем верну в двиг, но пока так.
        return;
    }
    else
    {
        float delta, min_zoom_factor;
        GetZoomData(m_fScopeZoomFactor, delta, min_zoom_factor);

        const float currentZoomFactor = m_fZoomFactor;

        if (Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system))
        {
            m_fZoomFactor += delta * (inc ? 1 : -1);
            clamp(m_fZoomFactor, min_zoom_factor, m_fScopeZoomFactor);
        }
        else
        {
            m_fZoomFactor -= delta * (inc ? 1 : -1);
            clamp(m_fZoomFactor, m_fScopeZoomFactor, min_zoom_factor);
        }

        wasChanged = !fsimilar(currentZoomFactor, m_fZoomFactor);

        if (H_Parent() && !IsRotatingToZoom())
            m_fRTZoomFactor = m_fZoomFactor; // store current
    }

    if (wasChanged)
    {
        OnZoomChanged();
    }
}

void CWeapon::SpawnAmmo(u32 boxCurr, LPCSTR ammoSect, u32 ParentID)
{
    if (!m_ammoTypes.size())
        return;

    if (!ammoSect)
        ammoSect = m_ammoTypes.front().c_str();

    CSE_Abstract* D = F_entity_Create(ammoSect);

    if (auto l_pA = smart_cast<CSE_ALifeItemAmmo*>(D))
    {
        l_pA->m_boxSize = (u16)pSettings->r_s32(ammoSect, "box_size");
        D->s_name = ammoSect;
        D->set_name_replace("");
        D->s_gameid = u8(GameID());
        D->s_RP = 0xff;
        D->ID = 0xffff;
        if (ParentID == 0xffffffff)
            D->ID_Parent = (u16)H_Parent()->ID();
        else
            D->ID_Parent = (u16)ParentID;

        D->ID_Phantom = 0xffff;
        D->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
        D->RespawnTime = 0;
        l_pA->m_tNodeID = ai_location().level_vertex_id();

        if (boxCurr == 0xffffffff)
            boxCurr = l_pA->m_boxSize;

        while (boxCurr)
        {
            l_pA->a_elapsed = (u16)(boxCurr > l_pA->m_boxSize ? l_pA->m_boxSize : boxCurr);
            NET_Packet P;
            D->Spawn_Write(P, TRUE);
            Level().Send(P, net_flags(TRUE));

            if (boxCurr > l_pA->m_boxSize)
                boxCurr -= l_pA->m_boxSize;
            else
                boxCurr = 0;
        }
    };
    F_entity_Destroy(D);
}

int CWeapon::GetAmmoCurrent(bool use_item_to_spawn) const
{
    int l_count = iAmmoElapsed;
    if (!m_pCurrentInventory)
        return l_count;

    //чтоб не делать лишних пересчетов
    if (m_pCurrentInventory->ModifyFrame() <= m_dwAmmoCurrentCalcFrame)
        return l_count + iAmmoCurrent;

    m_dwAmmoCurrentCalcFrame = Device.dwFrame;
    iAmmoCurrent = 0;

    for (int i = 0; i < (int)m_ammoTypes.size(); ++i)
    {
        iAmmoCurrent += GetAmmoCount_forType(m_ammoTypes[i]);

        if (!use_item_to_spawn)
            continue;

        if (!inventory_owner().item_to_spawn())
            continue;

        iAmmoCurrent += inventory_owner().ammo_in_box_to_spawn();
    }
    return l_count + iAmmoCurrent;
}

int CWeapon::GetAmmoCount(u8 ammo_type, u32 max) const
{
    VERIFY(m_pInventory);
    R_ASSERT(ammo_type < m_ammoTypes.size());

    return GetAmmoCount_forType(m_ammoTypes[ammo_type], max);
}

int CWeapon::GetAmmoCount_forType(shared_str const& ammo_type, u32 max) const
{
    u32 res = 0;
    auto callback = [&](const auto pIItem) -> bool {
        auto* ammo = smart_cast<CWeaponAmmo*>(pIItem);
        if (ammo->cNameSect() == ammo_type)
            res += ammo->m_boxCurr;
        return (max > 0 && res >= max);
    };

    m_pCurrentInventory->IterateAmmo(false, callback);
    if (max == 0 || res < max)
        if (!smart_cast<const CActor*>(H_Parent()) || !psActorFlags.test(AF_AMMO_ON_BELT))
            m_pCurrentInventory->IterateAmmo(true, callback);

    return res;
}

float CWeapon::GetConditionMisfireProbability() const
{
    if (GetCondition() > 0.95f)
        return 0.0f;

    float mis = misfireProbability + powf(1.f - GetCondition(), 3.f) * misfireConditionK;
    clamp(mis, 0.0f, 0.99f);
    return mis;
}

BOOL CWeapon::CheckForMisfire()
{
    if (!smart_cast<CActor*>(H_Parent())) // KRodin: НПС не нужны осечки.
        return FALSE;

    float rnd = ::Random.randF(0.f, 1.f);
    float mp = GetConditionMisfireProbability();
    if (rnd < mp)
    {
        FireEnd();
        SwitchMisfire(true);
        return TRUE;
    }
    else
        return FALSE;
}

void CWeapon::Reload() { OnZoomOut(); }

void CWeapon::DeviceSwitch() { OnZoomOut(); }

bool CWeapon::IsGrenadeLauncherAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eGrenadeLauncherStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eGrenadeLauncherStatus;
}

bool CWeapon::IsScopeAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eScopeStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eScopeStatus;
}

bool CWeapon::IsSilencerAttached() const
{
    return (CSE_ALifeItemWeapon::eAddonAttachable == m_eSilencerStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer)) ||
        CSE_ALifeItemWeapon::eAddonPermanent == m_eSilencerStatus;
}

bool CWeapon::GrenadeLauncherAttachable() const { return (CSE_ALifeItemWeapon::eAddonAttachable == m_eGrenadeLauncherStatus); }
bool CWeapon::ScopeAttachable() const { return (CSE_ALifeItemWeapon::eAddonAttachable == m_eScopeStatus); }
bool CWeapon::SilencerAttachable() const { return (CSE_ALifeItemWeapon::eAddonAttachable == m_eSilencerStatus); }

void CWeapon::UpdateHUDAddonsVisibility()
{
    if (!GetHUDmode())
        return;

    if (ScopeAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_scope_bones, IsScopeAttached());

    if (m_eScopeStatus == ALife::eAddonDisabled)
        HudItemData()->set_bone_visible(m_sHud_wpn_scope_bones, FALSE, TRUE);
    else if (m_eScopeStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_scope_bones, TRUE, TRUE);

    if (SilencerAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_silencer_bone, IsSilencerAttached());

    if (m_eSilencerStatus == ALife::eAddonDisabled)
        HudItemData()->set_bone_visible(m_sHud_wpn_silencer_bone, FALSE, TRUE);
    else if (m_eSilencerStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_silencer_bone, TRUE, TRUE);

    if (!HudItemData()->has_bone(m_sHud_wpn_launcher_bone) && HudItemData()->has_bone(wpn_launcher_def_bone_cop))
        m_sHud_wpn_launcher_bone = wpn_launcher_def_bone_cop;

    if (GrenadeLauncherAttachable())
        HudItemData()->set_bone_visible(m_sHud_wpn_launcher_bone, IsGrenadeLauncherAttached());

    if (m_eGrenadeLauncherStatus == ALife::eAddonDisabled)
        HudItemData()->set_bone_visible(m_sHud_wpn_launcher_bone, FALSE, TRUE);
    else if (m_eGrenadeLauncherStatus == ALife::eAddonPermanent)
        HudItemData()->set_bone_visible(m_sHud_wpn_launcher_bone, TRUE, TRUE);

    if (m_sHud_wpn_laser_bone.size() && has_laser)
        HudItemData()->set_bone_visible(m_sHud_wpn_laser_bone, IsLaserOn(), TRUE);

    if (m_sHud_wpn_flashlight_bone.size() && has_flashlight)
        HudItemData()->set_bone_visible(m_sHud_wpn_flashlight_bone, IsFlashlightOn(), TRUE);

    for (const shared_str& bone_name : hud_hidden_bones)
        HudItemData()->set_bone_visible(bone_name, FALSE, TRUE);

    callback(GameObject::eOnUpdateHUDAddonsVisibiility)();
}

void CWeapon::UpdateAddonsVisibility()
{
    auto pWeaponVisual = smart_cast<IKinematics*>(Visual());
    VERIFY(pWeaponVisual);

    UpdateHUDAddonsVisibility();

    ///////////////////////////////////////////////////////////////////
    u16 bone_id{};

    for (const auto& sbone : m_sWpn_scope_bones)
    {
        bone_id = pWeaponVisual->LL_BoneID(sbone);

        if (ScopeAttachable())
        {
            if (IsScopeAttached())
            {
                if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                    pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
            }
            else
            {
                if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                    pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
            }
        }

        if (m_eScopeStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
            pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        else if (m_eScopeStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
            pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
    }
    ///////////////////////////////////////////////////////////////////

    bone_id = pWeaponVisual->LL_BoneID(m_sWpn_silencer_bone);

    if (SilencerAttachable())
    {
        if (IsSilencerAttached())
        {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
        else
        {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }

    if (m_eSilencerStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    else if (m_eSilencerStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);

    ///////////////////////////////////////////////////////////////////

    bone_id = pWeaponVisual->LL_BoneID(m_sWpn_launcher_bone);

    if (GrenadeLauncherAttachable())
    {
        if (IsGrenadeLauncherAttached())
        {
            if (!pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
        else
        {
            if (pWeaponVisual->LL_GetBoneVisible(bone_id))
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
        }
    }

    if (m_eGrenadeLauncherStatus == CSE_ALifeItemWeapon::eAddonDisabled && bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    else if (m_eGrenadeLauncherStatus == CSE_ALifeItemWeapon::eAddonPermanent && bone_id != BI_NONE && !pWeaponVisual->LL_GetBoneVisible(bone_id))
        pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);

    ///////////////////////////////////////////////////////////////////

    if (m_sWpn_laser_bone.size() && has_laser)
    {
        bone_id = pWeaponVisual->LL_BoneID(m_sWpn_laser_bone);

        if (bone_id != BI_NONE)
        {
            const bool laser_on = IsLaserOn();
            if (pWeaponVisual->LL_GetBoneVisible(bone_id) && !laser_on)
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
            else if (!pWeaponVisual->LL_GetBoneVisible(bone_id) && laser_on)
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
    }

    ///////////////////////////////////////////////////////////////////

    if (m_sWpn_flashlight_bone.size() && has_flashlight)
    {
        bone_id = pWeaponVisual->LL_BoneID(m_sWpn_flashlight_bone);

        if (bone_id != BI_NONE)
        {
            const bool flashlight_on = IsFlashlightOn();
            if (pWeaponVisual->LL_GetBoneVisible(bone_id) && !flashlight_on)
                pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
            else if (!pWeaponVisual->LL_GetBoneVisible(bone_id) && flashlight_on)
                pWeaponVisual->LL_SetBoneVisible(bone_id, TRUE, TRUE);
        }
    }

    ///////////////////////////////////////////////////////////////////

    for (const auto& bone_name : hidden_bones)
    {
        bone_id = pWeaponVisual->LL_BoneID(bone_name);
        if (bone_id != BI_NONE && pWeaponVisual->LL_GetBoneVisible(bone_id))
            pWeaponVisual->LL_SetBoneVisible(bone_id, FALSE, TRUE);
    }

    ///////////////////////////////////////////////////////////////////

    callback(GameObject::eOnUpdateAddonsVisibiility)();

    pWeaponVisual->CalculateBones_Invalidate();
    pWeaponVisual->CalculateBones();
}

bool CWeapon::Activate(bool now)
{
    VisMask new_mask;
    new_mask.set_all();

    const auto K = smart_cast<IKinematics*>(Visual());
    K->LL_SetBonesVisible(new_mask);

    UpdateAddonsVisibility();
    return inherited::Activate(now);
}

void CWeapon::InitAddons() {}

float CWeapon::CurrentZoomFactor()
{
    if (Is3dssEnabled())
        return Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.0f : m_fIronSightZoomFactor; // no change to main fov zoom when use second vp
    else if (IsScopeAttached())
        return m_fScopeZoomFactor;
    else
        return m_fIronSightZoomFactor;
}

void CWeapon::OnZoomIn()
{
    m_bZoomMode = true;

    // если в режиме ПГ - не будем давать включать динамический зум
    if (m_bScopeDynamicZoom && !IsGrenadeMode() && !Is3dssEnabled())
        m_fZoomFactor = m_fRTZoomFactor;
    else
        m_fZoomFactor = CurrentZoomFactor();

    if (IsScopeAttached() && !IsGrenadeMode())
    {
        if (!m_bScopeZoomInertionAllow)
            AllowHudInertion(FALSE);
    }
    else if (!m_bZoomInertionAllow)
        AllowHudInertion(FALSE);

    if (smart_cast<CActor*>(H_Parent()))
        g_actor->callback(GameObject::eOnActorWeaponZoomIn)(lua_game_object());

    g_player_hud->updateMovementLayerState();
}

void CWeapon::OnZoomOut()
{
    m_fZoomFactor = Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.f : g_fov;

    if (m_bZoomMode)
    {
        SprintType = false;
        m_bZoomMode = false;

        if (smart_cast<CActor*>(H_Parent()))
        {
            g_actor->callback(GameObject::eOnActorWeaponZoomOut)(lua_game_object());
        }
    }

    AllowHudInertion(TRUE);

    ResetSubStateTime();

    g_player_hud->updateMovementLayerState();
}

bool CWeapon::UseScopeTexture()
{
    return !Is3dssEnabled() && m_UIScope; // только если есть текстура прицела - для простого создания коллиматоров
}

CUIStaticItem* CWeapon::ZoomTexture()
{
    if (UseScopeTexture())
        return m_UIScope;
    else
        return NULL;
}

void CWeapon::SwitchState(u32 S)
{
    SetNextState(S); // Very-very important line of code!!! :)
    if (CHudItem::object().Local() && !CHudItem::object().getDestroy() /* && (S!=NEXT_STATE)*/
        && m_pCurrentInventory)
    {
        // !!! Just single entry for given state !!!
        NET_Packet P;
        CHudItem::object().u_EventGen(P, GE_WPN_STATE_CHANGE, CHudItem::object().ID());
        P.w_u8(u8(S));
        P.w_u8(u8(m_sub_state));
        P.w_u8(u8(m_set_next_ammoType_on_reload & 0xff));
        CHudItem::object().u_EventSend(P, net_flags(TRUE, TRUE, FALSE, TRUE));
    }
}

void CWeapon::OnMagazineEmpty() { VERIFY((u32)iAmmoElapsed == m_magazine.size()); }

void CWeapon::reinit()
{
    CShootingObject::reinit();
    CHudItemObject::reinit();
}

void CWeapon::reload(LPCSTR section)
{
    CShootingObject::reload(section);
    CHudItemObject::reload(section);

    m_can_be_strapped = true;
    m_strapped_mode = false;

    if (pSettings->line_exist(section, "strap_bone0"))
        m_strap_bone0 = pSettings->r_string(section, "strap_bone0");
    else
        m_can_be_strapped = false;

    if (pSettings->line_exist(section, "strap_bone1"))
        m_strap_bone1 = pSettings->r_string(section, "strap_bone1");
    else
        m_can_be_strapped = false;

    if (m_eScopeStatus == ALife::eAddonAttachable)
    {
        m_addon_holder_range_modifier = READ_IF_EXISTS(pSettings, r_float, m_sScopeName, "holder_range_modifier", m_holder_range_modifier);
        m_addon_holder_fov_modifier = READ_IF_EXISTS(pSettings, r_float, m_sScopeName, "holder_fov_modifier", m_holder_fov_modifier);
    }
    else
    {
        m_addon_holder_range_modifier = m_holder_range_modifier;
        m_addon_holder_fov_modifier = m_holder_fov_modifier;
    }

    {
        Fvector pos, ypr;
        pos = pSettings->r_fvector3(section, "position");
        ypr = pSettings->r_fvector3(section, "orientation");
        ypr.mul(PI / 180.f);

        m_Offset.setHPB(ypr.x, ypr.y, ypr.z);
        m_Offset.translate_over(pos);
    }

    m_StrapOffset = m_Offset;
    if (pSettings->line_exist(section, "strap_position") && pSettings->line_exist(section, "strap_orientation"))
    {
        Fvector pos, ypr;
        pos = pSettings->r_fvector3(section, "strap_position");
        ypr = pSettings->r_fvector3(section, "strap_orientation");
        ypr.mul(PI / 180.f);

        m_StrapOffset.setHPB(ypr.x, ypr.y, ypr.z);
        m_StrapOffset.translate_over(pos);
    }
    else
        m_can_be_strapped = false;

    m_ef_main_weapon_type = READ_IF_EXISTS(pSettings, r_u32, section, "ef_main_weapon_type", u32(-1));
    m_ef_weapon_type = READ_IF_EXISTS(pSettings, r_u32, section, "ef_weapon_type", u32(-1));
}

void CWeapon::create_physic_shell()
{
    // xrKrodin: Временный? "фикс" для оружия из ганслингера, валяющегося на земле. По непонятным причинам (много костей или хз от чего ещё) в некоторых случаях при рассчетах
    // физики происходят краши в ode которые исправить невозможно.
    if (is_gunslinger_weapon || IS_OGSR_GA)
    {
        auto vis = smart_cast<IKinematics*>(Visual());
        auto& rootBoneData = vis->LL_GetData(vis->LL_GetBoneRoot());
        m_pPhysicsShell = P_build_SimpleShell(this, rootBoneData.mass, false);
        m_pPhysicsShell->SetMaterial(rootBoneData.game_mtl_idx);
    }
    else
        CPhysicsShellHolder::create_physic_shell();
}

void CWeapon::activate_physic_shell() { CPhysicsShellHolder::activate_physic_shell(); }

void CWeapon::setup_physic_shell() { CPhysicsShellHolder::setup_physic_shell(); }

bool CWeapon::can_kill() const
{
    if (GetAmmoCurrent(true) || m_ammoTypes.empty())
        return (true);

    return (false);
}

CInventoryItem* CWeapon::can_kill(CInventory* inventory) const
{
    if (GetAmmoElapsed() || m_ammoTypes.empty())
        return (const_cast<CWeapon*>(this));

    TIItemContainer::iterator I = inventory->m_all.begin();
    TIItemContainer::iterator E = inventory->m_all.end();
    for (; I != E; ++I)
    {
        CInventoryItem* inventory_item = smart_cast<CInventoryItem*>(*I);
        if (!inventory_item)
            continue;

        xr_vector<shared_str>::const_iterator i = std::find(m_ammoTypes.begin(), m_ammoTypes.end(), inventory_item->object().cNameSect());
        if (i != m_ammoTypes.end())
            return (inventory_item);
    }

    return (0);
}

const CInventoryItem* CWeapon::can_kill(const xr_vector<const CGameObject*>& items) const
{
    if (m_ammoTypes.empty())
        return (this);

    xr_vector<const CGameObject*>::const_iterator I = items.begin();
    xr_vector<const CGameObject*>::const_iterator E = items.end();
    for (; I != E; ++I)
    {
        const CInventoryItem* inventory_item = smart_cast<const CInventoryItem*>(*I);
        if (!inventory_item)
            continue;

        xr_vector<shared_str>::const_iterator i = std::find(m_ammoTypes.begin(), m_ammoTypes.end(), inventory_item->object().cNameSect());
        if (i != m_ammoTypes.end())
            return (inventory_item);
    }

    return (0);
}

bool CWeapon::ready_to_kill() const { return (!IsMisfire() && ((GetState() == eIdle) || (GetState() == eFire) || (GetState() == eFire2)) && GetAmmoElapsed()); }

// Получить индекс текущих координат худа
u8 CWeapon::GetCurrentHudOffsetIdx() const
{
    if (IsZoomed())
    {
        const bool has_gl = GrenadeLauncherAttachable() && IsGrenadeLauncherAttached();
        const bool has_scope = ScopeAttachable() && IsScopeAttached();
        //const bool has_aim_alt = AimAlt && is_second_zoom_offset_enabled;

        if (IsGrenadeMode())
        {
            if (m_bUseScopeGrenadeZoom && has_scope)
                return hud_item_measures::m_hands_offset_type_gl_scope;
            else
                return hud_item_measures::m_hands_offset_type_gl;
        }
        else if (has_gl)
        {
            if (m_bUseScopeZoom && has_scope)
                return hud_item_measures::m_hands_offset_type_gl_normal_scope;
            else
                return hud_item_measures::m_hands_offset_type_aim_gl_normal;
        }
        else
        {
            if (m_bUseScopeZoom && has_scope)
                return hud_item_measures::m_hands_offset_type_aim_scope;
            //else if (has_aim_alt)
            //    return hud_item_measures::m_hands_offset_type_alt_aim;
            else
                return hud_item_measures::m_hands_offset_type_aim;
        }
    }

    //if (LoweredActive && !(g_actor->get_state() & mcSprint))
    //    return hud_item_measures::m_hands_offset_type_lowered;

    return hud_item_measures::m_hands_offset_type_normal;
}

void CWeapon::SetAmmoElapsed(int ammo_count)
{
    iAmmoElapsed = ammo_count;

    u32 uAmmo = u32(iAmmoElapsed);

    if (uAmmo != m_magazine.size())
    {
        if (uAmmo > m_magazine.size())
        {
            CCartridge l_cartridge;
            l_cartridge.Load(*m_ammoTypes[m_ammoType], u8(m_ammoType));
            while (uAmmo > m_magazine.size())
                m_magazine.push_back(l_cartridge);
        }
        else
        {
            while (uAmmo < m_magazine.size())
                m_magazine.pop_back();
        };
    };
}

u32 CWeapon::ef_main_weapon_type() const
{
    VERIFY(m_ef_main_weapon_type != u32(-1));
    return (m_ef_main_weapon_type);
}

u32 CWeapon::ef_weapon_type() const
{
    VERIFY(m_ef_weapon_type != u32(-1));
    return (m_ef_weapon_type);
}

bool CWeapon::IsNecessaryItem(const shared_str& item_sect) { return (std::find(m_ammoTypes.begin(), m_ammoTypes.end(), item_sect) != m_ammoTypes.end()); }

void CWeapon::modify_holder_params(float& range, float& fov) const
{
    if (!IsScopeAttached())
    {
        inherited::modify_holder_params(range, fov);
        return;
    }
    range *= m_addon_holder_range_modifier;
    fov *= m_addon_holder_fov_modifier;
}

void CWeapon::OnDrawUI()
{
    if (IsZoomed() && ZoomHideCrosshair())
    {
        if (ZoomTexture() && !IsRotatingToZoom())
        {
            ZoomTexture()->SetPos(0, 0);
            ZoomTexture()->SetRect(0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
            ZoomTexture()->Render();

            //			m_UILens.Draw();
        }
    }
}

bool CWeapon::IsHudModeNow() { return (HudItemData() != nullptr); }

bool CWeapon::unlimited_ammo()
{
    if (m_pCurrentInventory)
        return inventory_owner().unlimited_ammo() && m_DefaultCartridge.m_flags.test(CCartridge::cfCanBeUnlimited);
    else
        return false;
};

LPCSTR CWeapon::GetCurrentAmmo_ShortName()
{
    if (m_magazine.empty())
        return ("");
    CCartridge& l_cartridge = m_magazine.back();
    return *(l_cartridge.m_InvShortName);
}

float CWeapon::GetCurrentAmmoDisp()
{
    if (m_magazine.empty())
        return (3);
    CCartridge& l_cartridge = m_magazine.back();
    return (l_cartridge.m_kDisp);
}

float CWeapon::GetMagazineWeight(const decltype(CWeapon::m_magazine)& mag) const
{
    float res = 0;
    const char* last_type = nullptr;
    float last_ammo_weight = 0;
    for (auto& c : mag)
    {
        // Usually ammos in mag have same type, use this fact to improve performance
        if (last_type != c.m_ammoSect.c_str())
        {
            last_type = c.m_ammoSect.c_str();
            last_ammo_weight = c.Weight();
        }
        res += last_ammo_weight;
    }
    return res;
}

float CWeapon::Weight() const
{
    float res = CInventoryItemObject::Weight();
    if (GrenadeLauncherAttachable() && IsGrenadeLauncherAttached())
        res += pSettings->r_float(GetGrenadeLauncherName(), "inv_weight");
    if (ScopeAttachable() && IsScopeAttached())
        res += pSettings->r_float(GetScopeName(), "inv_weight");
    if (SilencerAttachable() && IsSilencerAttached())
        res += pSettings->r_float(GetSilencerName(), "inv_weight");
    res += GetMagazineWeight(m_magazine);

    return res;
}

u32 CWeapon::Cost() const
{
    u32 res = m_cost;

    if (Core.Features.test(xrCore::Feature::wpn_cost_include_addons))
    {
        if (GrenadeLauncherAttachable() && IsGrenadeLauncherAttached())
            res += pSettings->r_u32(GetGrenadeLauncherName(), "cost");
        if (ScopeAttachable() && IsScopeAttached())
            res += pSettings->r_u32(GetScopeName(), "cost");
        if (SilencerAttachable() && IsSilencerAttached())
            res += pSettings->r_u32(GetSilencerName(), "cost");
    }
    return res;
}

void CWeapon::Hide(bool now)
{
    if (now)
    {
        OnStateSwitch(eHidden, GetState());
        SetState(eHidden);
        StopHUDSounds();
    }
    else
        SwitchState(eHiding);

    OnZoomOut();
}

void CWeapon::Show(bool now)
{
    if (now)
    {
        StopCurrentAnimWithoutCallback();
        OnStateSwitch(eIdle, GetState());
        SetState(eIdle);
        StopHUDSounds();
    }
    else
        SwitchState(eShowing);
}

bool CWeapon::show_crosshair() { return psActorFlags.test(AF_CROSSHAIR_DBG) || !(IsZoomed() && ZoomHideCrosshair()); }

bool CWeapon::show_indicators() { return !(IsZoomed() && (ZoomTexture() || !m_bScopeShowIndicators)); }

float CWeapon::GetConditionToShow() const
{
    return (GetCondition()); // powf(GetCondition(),4.0f));
}

bool CWeapon::ParentIsActor() const
{
    return smart_cast<const CActor*>(H_Parent()) != nullptr;
}

const float& CWeapon::hit_probability() const
{
    VERIFY((g_SingleGameDifficulty >= egdNovice) && (g_SingleGameDifficulty <= egdMaster));
#pragma todo("WTF???")
    return (m_hit_probability[egdNovice]);
}

bool CWeapon::Is3dssEnabled() const
{
    const auto& zoom_params = shader_exports.get_custom_params("s3ds_param_2");
    return !fis_zero(zoom_params.w) && !IsGrenadeMode() && psActorFlags.test(AF_3D_SCOPES);
}

// Чувствительность мышки с оружием в руках во время прицеливания
float CWeapon::GetControlInertionFactor() const
{
    const float fInertionFactor = inherited::GetControlInertionFactor();

    if (IsZoomed() && Is3dssEnabled() && !IsRotatingToZoom())
    {
        if (m_bScopeDynamicZoom)
        {
            const auto& zoom_params = shader_exports.get_custom_params("s3ds_param_2");
            const float& max_zoom = zoom_params.y;
            const float& current_zoom = zoom_params.w;
            const float res = fInertionFactor + ((m_fScopeInertionFactor - fInertionFactor) * (current_zoom / max_zoom));
            // Msg("--[%s] current inertion: [%g], fInertionFactor: [%g], m_fScopeInertionFactor: [%g], current zoom: [%g], max_zoom: [%g]", __FUNCTION__, res, fInertionFactor,
            // m_fScopeInertionFactor, current_zoom, max_zoom);
            return res;
        }
        else
            return m_fScopeInertionFactor;
    }

    return fInertionFactor;
}

float CWeapon::GetHudFov()
{
    const float last_nw_hf = inherited::GetHudFov();

    if (m_fZoomRotationFactor > 0.0f)
    {
        if (Is3dssEnabled() && m_f3dssHudFov > 0.0f)
        {
            // В линзе зума
            const float fDiff = last_nw_hf - m_f3dssHudFov;
            return m_f3dssHudFov + (fDiff * (1 - m_fZoomRotationFactor));
        }
        if ((m_eScopeStatus == CSE_ALifeItemWeapon::eAddonDisabled || IsScopeAttached()) && !IsGrenadeMode() && m_fZoomHudFov > 0.0f)
        {
            // В процессе зума
            const float fDiff = last_nw_hf - m_fZoomHudFov;
            return m_fZoomHudFov + (fDiff * (1 - m_fZoomRotationFactor));
        }
    }

    return last_nw_hf;
}

void CWeapon::OnBulletHit()
{
    if (!fis_zero(conditionDecreasePerShotOnHit))
        ChangeCondition(-conditionDecreasePerShotOnHit);
}

// По ef_weapon_type тут проверяем, пулемёт ли это. Это костыль чтоб при смене типа патронов не играла анимация reload_empty, которая выглядит в данном случае неправильно.
bool CWeapon::IsPartlyReloading() const { return (ef_weapon_type() == 10 || m_set_next_ammoType_on_reload == u32(-1)) && GetAmmoElapsed() > 0 && !IsMisfire(); }

void CWeapon::SaveAttachableParams()
{
    const char* sect_name = cNameSect().c_str();
    string_path buff;
    FS.update_path(buff, "$logs$", make_string("_world\\%s.ltx", sect_name).c_str());

    CInifile pHudCfg(buff, FALSE, FALSE, TRUE);

    sprintf_s(buff, "%f,%f,%f", m_Offset.c.x, m_Offset.c.y, m_Offset.c.z);
    pHudCfg.w_string(sect_name, "position", buff);

    Fvector ypr;
    m_Offset.getHPB(ypr.x, ypr.y, ypr.z);
    ypr.mul(180.f / PI);
    sprintf_s(buff, "%f,%f,%f", ypr.x, ypr.y, ypr.z);
    pHudCfg.w_string(sect_name, "orientation", buff);

    if (pSettings->line_exist(sect_name, "strap_position") && pSettings->line_exist(sect_name, "strap_orientation"))
    {
        sprintf_s(buff, "%f,%f,%f", m_StrapOffset.c.x, m_StrapOffset.c.y, m_StrapOffset.c.z);
        pHudCfg.w_string(sect_name, "strap_position", buff);
        m_StrapOffset.getHPB(ypr.x, ypr.y, ypr.z);
        ypr.mul(180.f / PI);
        sprintf_s(buff, "%f,%f,%f", ypr.x, ypr.y, ypr.z);
        pHudCfg.w_string(sect_name, "strap_orientation", buff);
    }

    Msg("--[%s] data saved to [%s]", __FUNCTION__, pHudCfg.fname());
}

void CWeapon::UpdateVisualBullets()
{
    if (!bullet_update)
        return;

    if (bHasBulletsToHide)
    {
        const int AE = GetAmmoElapsed();

        last_hide_bullet = AE >= bullet_cnt ? bullet_cnt : (AE == 0 ? -1 : bullet_cnt - AE - 1);
    }

    HUD_VisualBulletUpdate();
}

void CWeapon::HUD_VisualBulletUpdate(bool force, int force_idx)
{
    if (!GetHUDmode())
        return;

    if (!bHasBulletsToHide)
        return;

    bool hide = true;

    // Msg("Print %d bullets", last_hide_bullet);

    if (last_hide_bullet == bullet_cnt || force)
        hide = false;

    for (auto b = 0; b < bullet_cnt; b++)
    {
        const auto bone_id = HudItemData()->m_model->LL_BoneID(bullets_bones[b]);

        if (bone_id != BI_NONE)
            HudItemData()->set_bone_visible(bullets_bones[b], !hide);

        if (b == last_hide_bullet)
            hide = false;
    }
}

void CWeapon::ParseCurrentItem(CGameFont* F) { F->OutNext("WEAPON IN STRAPPED MODE: [%d]", m_strapped_mode); }


void CWeapon::ChangeFP(float fp)
{
    Msg("reload ammo types 2");
    m_ammoTypes.clear();
    LPCSTR S = "ammo_12x70_buck";
    if (S && S[0])
    {
        string128 _ammoItem;
        int count = _GetItemCount(S);
        for (int it = 0; it < count; ++it)
        {
            _GetItem(S, it, _ammoItem);
            m_ammoTypes.push_back(_ammoItem);
        }
    }
}
void CWeapon::update_visual_bullet_textures(const bool forced)
{
    if (bullet_textures_in_model.empty())
        return;

    if (!GetHUDmode())
        return;

    const u32 id = m_set_next_ammoType_on_reload != u32(-1) ? m_set_next_ammoType_on_reload : m_ammoType;
    const auto& current_ammo_sect = m_ammoTypes[id];
    const auto bullet_texrure_find_it = bullet_textures_for_ammos.find(current_ammo_sect);
    ASSERT_FMT(bullet_texrure_find_it != bullet_textures_for_ammos.end(), "!!Can't find [%s] in [bullet_textures_for_ammos] of [%s]", current_ammo_sect.c_str(),
               cNameSect().c_str());
    const auto& bullet_texrure_name = bullet_texrure_find_it->second;

    if (!forced && current_bullet_texture == bullet_texrure_name)
        return;

    for (const auto& tex_name : bullet_textures_in_model)
    {
        const auto textures = Device.m_pRender->GetResourceManager()->FindTexture(tex_name.c_str());
        if (textures.empty())
        {
            Msg("!![%s] can't find texture [%s] for [%s]", __FUNCTION__, tex_name.c_str(), cNameSect().c_str());
            continue;
        }

        auto* tex = textures.front();
        tex->Unload();
        tex->Load(bullet_texrure_name.c_str());
        current_bullet_texture = bullet_texrure_name;
        //Msg("--[%s] replaced texture [%s] --> [%s] for [%s]", __FUNCTION__, tex_name.c_str(), current_bullet_texture.c_str(), cNameSect().c_str());

    }
}

void CWeapon::on_a_hud_attach()
{
    inherited::on_a_hud_attach();

    VisMask new_mask;
    new_mask.set_all();

    IKinematics* K = HudItemData()->m_model;
    K->LL_SetBonesVisible(new_mask);
}

void CWeapon::on_b_hud_detach() { inherited::on_b_hud_detach(); }
