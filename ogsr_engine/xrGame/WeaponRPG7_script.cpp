#include "stdafx.h"
#include "WeaponRPG7.h"

using namespace luabind;


void CWeaponRPG7::script_register(lua_State* L) { module(L)[class_<CWeaponRPG7, CGameObject>("CWeaponRPG7").def(constructor<>())]; }
