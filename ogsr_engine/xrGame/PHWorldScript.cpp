#include "stdafx.h"
#include "Physics.h"
#include "PHWorld.h"
#include "PHCommander.h"

using namespace luabind;


void CPHWorld::script_register(lua_State* L)
{
    module(L)[class_<CPHWorld>("physics_world").def("set_gravity", &CPHWorld::SetGravity).def("gravity", &CPHWorld::Gravity).def("add_call", &CPHWorld::AddCall)];
}
