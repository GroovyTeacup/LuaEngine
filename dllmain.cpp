#include <fstream>
#include <queue>
#include <functional>

#include <random>
#include <iostream>
#include <cmath>

#include <windows.h>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include "minhook/include/MinHook.h"
#include "json/json.hpp"
#include "sol/sol.hpp"
#include "loader.h"
#include "ghidra_export.h"
#include "util.h"
#include <thread>

#include "game_utils.h"
#include "lua_core.h"

#include "hook_monster.h"
#include "hook_animals.h"
#include "hook_camera.h"
#include "hook_hit.h"
#include "hook_shlp.h"
#include "hook_frame.h"

#include "hook_ui.h"

using namespace loader;

#pragma region Basic data processing program
static void MassageCommand() {
	void* MassagePlot = *(undefined**)MH::World::Message;
	if (MassagePlot != nullptr) {
		string Massage = offsetPtr<char>(MassagePlot, 0xC0);
		string::size_type idx;
		//执行实时命令
		idx = Massage.find("luac:");
		if (idx != string::npos) {
			string command = Massage.substr(Massage.find("luac:") + 5);
			framework_logger->info("执行游戏内发出的Lua实时指令: {}", command);
			int err = 0;
			err = luaL_dostring(LuaHandle::Lc, command.c_str());
			if (err != 0)
			{
				int type = lua_type(LuaHandle::Lc, -1);
				if (type == 4) {
					string error = lua_tostring(LuaHandle::Lc, -1);
					LuaCore::LuaErrorRecord(error);
				}
			}
			*offsetPtr<char>(MassagePlot, 0xC0) = *"";
		}
		//重载虚拟机
		idx = Massage.find("reload ");
		if (idx != string::npos) {
			string luae = Massage.substr(Massage.find("reload ") + 7);
			std::map<string, LuaHandle::LuaScriptData>::iterator it;
			it = LuaHandle::LuaScript.find(luae);
			if (it != LuaHandle::LuaScript.end()) {
				framework_logger->info("重载脚本{}的Lua虚拟机", luae);
				lua_close(LuaHandle::LuaScript[luae].L);
				LuaHandle::LuaScript[luae].L = luaL_newstate();
				luaL_openlibs(LuaHandle::LuaScript[luae].L);
				registerFunc(LuaHandle::LuaScript[luae].L);
				hook_monster::Registe(LuaHandle::LuaScript[luae].L);
				hook_animals::Registe(LuaHandle::LuaScript[luae].L);
				hook_camera::Registe(LuaHandle::LuaScript[luae].L);
				hook_hit::Registe(LuaHandle::LuaScript[luae].L);
				hook_frame::Registe(LuaHandle::LuaScript[luae].L);
				hook_shlp::Registe(LuaHandle::LuaScript[luae].L);
				//hook_screen::Registe(LuaHandle::LuaScript[luae].L);
				hook_ui::LuaRegister(LuaHandle::LuaScript[luae].L);
				if (LuaCore::Lua_Run(LuaHandle::LuaScript[luae].L, luae) != 1) {
					engine_logger->warn("脚本{}重载后出现异常，已停止该脚本继续运行", luae);
					LuaHandle::LuaScript[luae].start = false;
				}
				else {
					engine_logger->info("脚本{}已完成重载操作，代码运行正常", luae);
					string message = "脚本" + luae + "已完成重载操作";
					MH::Chat::ShowGameMessage(*(undefined**)MH::Chat::MainPtr, (undefined*)&utils::string_To_UTF8(message)[0], -1, -1, 0);
					LuaHandle::LuaScript[luae].start = true;
					LuaCore::run("on_init", LuaHandle::LuaScript[luae].L);
				}
			}
			*offsetPtr<char>(MassagePlot, 0xC0) = *"";
		}
	}
}
//数据更新程序
static void updata() {
	//地图更新时清理数据
	void* TimePlot = utils::GetPlot(*(undefined**)MH::Player::PlayerBasePlot, { 0x50, 0x7D20 });
	if (TimePlot != nullptr && Chronoscope::NowTime > *offsetPtr<float>(TimePlot, 0xC24)) {
		framework_logger->info("游戏内发生场景变换，更新框架缓存数据");
		//清除计时器数据
		Chronoscope::ChronoscopeList.clear();
		//清除按键数据
		Keyboard::TempData::t_KeyCount.clear();
		Keyboard::TempData::t_KeyDown.clear();
		//清除Xbox手柄数据
		XboxPad::TempData::t_KeyCount.clear();
		XboxPad::TempData::t_KeyDown.clear();
		//钩子数据
		hook_frame::SpeedList.clear();
		engine_logger->info("运行脚本on_switch_scenes场景切换代码");
		LuaCore::run("on_switch_scenes");
	}
	//更新计时器
	Chronoscope::chronoscope();
	//更新手柄数据
	XboxPad::Updata();
	//执行玩家消息指令
	MassageCommand();
}
#pragma endregion

__declspec(dllexport) extern bool Load()
{
	engine_logger->set_level(spdlog::level::info);
	engine_logger->flush_on(spdlog::level::trace);
	framework_logger->set_level(spdlog::level::info);
	framework_logger->flush_on(spdlog::level::trace);
	lua_logger->set_level(spdlog::level::info);
	lua_logger->flush_on(spdlog::level::trace);
	imgui_logger->set_level(spdlog::level::info);
	imgui_logger->flush_on(spdlog::level::trace);
	//加载lua文件
	LuaCore::Lua_Load("Lua\\", LuaHandle::LuaFiles);
	//脚本实时环境
	framework_logger->info("创建并运行实时脚本环境");
	LuaHandle::Lc = luaL_newstate();
	luaL_openlibs(LuaHandle::Lc);
	registerFunc(LuaHandle::Lc);
	//加载并运行脚本
	for (string file_name : LuaHandle::LuaFiles) {
		if (LuaHandle::LuaScript[file_name].start && LuaHandle::LuaScript[file_name].L != nullptr) {
			lua_State* L = LuaHandle::LuaScript[file_name].L;
			luaL_openlibs(L);
			engine_logger->info("为{}脚本注册引擎初始化函数", file_name);
			registerFunc(L);
			hook_monster::Registe(L);
			hook_animals::Registe(L);
			hook_camera::Registe(L);
			hook_hit::Registe(L);
			hook_frame::Registe(L);
			hook_shlp::Registe(L);
			//hook_screen::Registe(L);
			hook_ui::LuaRegister(L);
			if (LuaCore::Lua_Run(L, file_name) != 1) {
				engine_logger->warn("脚本{}运行过程中出现异常，已停止该脚本继续运行", file_name);
				LuaHandle::LuaScript[file_name].start = false;
			}
		}
	}
	//其他绑定操作
	framework_logger->info("开始执行创建各功能钩子代码");
	hook_monster::Hook();
	hook_animals::Hook();
	hook_camera::Hook();
	hook_hit::Hook();
	hook_shlp::Hook();
	hook_frame::Hook();
	//hook_screen::Hook();
	//运行lua脚本中初始化代码
	engine_logger->info("运行脚本on_init初始化代码");
	LuaCore::run("on_init");
	//初始化钩子
	framework_logger->info("创建on_time钩子");
	MH_Initialize();
	HookLambda(MH::World::MapClockLocal,
		[](auto clock, auto clock2) {
			auto ret = original(clock, clock2);
			//更新基础数据
			updata();
			//运行lua虚拟机
			LuaCore::run("on_time");
			//hook_ui::init();
			//启动绘制更新
			if(!LuaCore::luaframe)
				LuaCore::luaframe = true;
			return ret;
		});
	MH_ApplyQueued();
	return true;
}

#define MAX_LINE 1024
#define MAX_SECTION 50
#define MAX_KEY 50
#define MAX_VALUE 50
// 从一行字符串中解析出节、键和值，返回类型：0-空行或注释，1-节，2-键值对，-1-错误
int parse_line(char* line, char* section, char* key, char* value)
{
	char* start, * end;
	line[strlen(line) - 1] = '\0'; // 去掉换行符
	start = line; // 起始位置
	while (*start == ' ') start++; // 跳过空格
	if (*start == '\0' || *start == ';') return 0; // 空行或注释
	if (*start == '[') // 节
	{
		start++;
		end = strchr(start, ']');
		if (end == NULL) return -1; // 没有找到右括号
		strncpy_s(section, MAX_SECTION, start, end - start); // 复制节名
		section[end - start] = '\0';
		return 1;
	}
	else // 键值对
	{
		end = strchr(start, '=');
		if (end == NULL) return -1; // 没有找到等号
		strncpy_s(key, MAX_KEY, start, end - start); // 复制键名
		key[end - start] = '\0';
		start = end + 1;
		strcpy_s(value, MAX_VALUE, start); // 复制值
		return 2;
	}
}

// 从ini文件中获取指定节和键的值，如果成功返回1，否则返回0
int get_value_from_ini(const char* file_name, const char* section, const char* key, char* value)
{
	FILE* fp;
	char line[MAX_LINE];
	char cur_section[MAX_SECTION] = "";
	int found = 0; // 是否找到指定节和键
	fopen_s(&fp, file_name, "r"); // 打开文件
	if (fp == NULL) // 文件不存在
	{
		return 0;
	}
	while (fgets(line, MAX_LINE, fp) != NULL) // 逐行读取
	{
		char tmp_section[MAX_SECTION], tmp_key[MAX_KEY], tmp_value[MAX_VALUE];
		int type = parse_line(line, tmp_section, tmp_key, tmp_value); // 解析行
		if (type == 1) // 节
		{
			strcpy_s(cur_section, MAX_SECTION, tmp_section); // 更新当前节
		}
		else if (type == 2) // 键值对
		{
			if (strcmp(cur_section, section) == 0 && strcmp(tmp_key, key) == 0) // 找到指定节和键
			{
				strcpy_s(value, MAX_VALUE, tmp_value); // 复制值
				found = 1; // 标记已找到
				break; // 退出循环
			}
		}
	}
	fclose(fp); // 关闭文件
	return found;
}
DWORD WINAPI AttachThread(LPVOID lParam) {
	char DX12Api[MAX_VALUE];
	if (get_value_from_ini("graphics_option.ini", "GraphicsOption", "DirectX12Enable", DX12Api))
	{
		if (string (DX12Api) == "On") {
			framework_logger->info("DirectX12 模式");
			hook_ui::dx12API = true;
		}
		else {
			framework_logger->info("DirectX11 模式");
			hook_ui::dx12API = false;
		}
	}
	else {
		hook_ui::dx12API = false;
	}
	hook_ui::init();
	return 0;
}
BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH: {
			DisableThreadLibraryCalls(hModule);
			hook_ui::hMod = hModule;
			CreateThread(nullptr, 0, &AttachThread, static_cast<LPVOID>(hModule), 0, nullptr);
			Load();
			break;
		}
		case DLL_PROCESS_DETACH: {
			hook_ui::removeUI();
			break;
		}
	}
	return TRUE;
}

