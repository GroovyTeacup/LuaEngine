#include <fstream>
#include <queue>
#include <functional>

#include <random>
#include <iostream>
#include <cmath>

#include <windows.h>

#include "minhook/include/MinHook.h"
#include "json/json.hpp"
#include "sol/sol.hpp"
#include "loader.h"
#include "ghidra_export.h"
#include "util.h"
#include <thread>

#include "game_utils.h"
#include "lua_core.h"

using namespace loader;

#pragma region Basic data processing program
//数据更新程序
static void updata() {
	//地图更新时清理数据
	void* TimePlot = utils::GetPlot(*(undefined**)MH::Player::PlayerBasePlot, { 0x50, 0x88, 0x1B0, 0x308, 0x10, 0x10 });
	if (Chronoscope::NowTime > *offsetPtr<float>(TimePlot, 0xC24)) {
		//清除计时器数据
		Chronoscope::ChronoscopeList.clear();
		//清除按键数据
		Keyboard::TempData::t_KeyCount.clear();
		Keyboard::TempData::t_KeyDown.clear();
		//清除Xbox手柄数据
		XboxPad::TempData::t_KeyCount.clear();
		XboxPad::TempData::t_KeyDown.clear();
		run("on_switch_scenes");
	}
	//更新计时器
	Chronoscope::chronoscope();
	//更新手柄数据
	XboxPad::Updata();
}
#pragma endregion

//运行lua代码
static void run(string func) {
	for (string file_name : LuaHandle::LuaFiles) {
		if (LuaHandle::LuaScript[file_name].start) {
			lua_State* L = LuaHandle::LuaScript[file_name].L;
			int err = 0;
			int callBack = lua_gettop(L);
			lua_getglobal(L, func.c_str());
			err = lua_pcall(L, 0, 0, callBack);
			if (err != 0)
			{
				string error = lua_tostring(L, -1);
				LuaCore::LuaErrorRecord(error);
			}
		}
	}
}

__declspec(dllexport) extern bool Load()
{
	//加载lua文件
	LuaCore::Lua_Load("Lua\\", LuaHandle::LuaFiles);
	LOG(INFO) << "Lua file load:";
	//运行脚本
	for (string file_name : LuaHandle::LuaFiles) {
		if (LuaHandle::LuaScript[file_name].start && LuaHandle::LuaScript[file_name].L != nullptr) {
			lua_State* L = LuaHandle::LuaScript[file_name].L;
			luaL_openlibs(L);
			registerFunc(L);
			if (LuaCore::Lua_Run(L, file_name) != 1) {
				LuaHandle::LuaScript[file_name].start = false;
			}
		}
	}
	//初始化钩子
	MH_Initialize();
	HookLambda(MH::World::MapClockLocal,
		[](auto clock, auto clock2) {
			auto ret = original(clock, clock2);
			//更新基础数据
			updata();
			//运行lua虚拟机
			run("on_time");
			return ret;
		});
	MH_ApplyQueued();
	return true;
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
		return Load();
	return TRUE;
}

