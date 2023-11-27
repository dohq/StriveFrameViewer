#include <polyhook2/Detour/x64Detour.hpp>
#include "sigscan.h"
#include "arcsys.h"
#include "netcalls.h"

#include <UE4SSProgram.hpp>
#include <UnrealDef.hpp>
#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#define WIN32_LEAN_AND_MEAN
#include "Windows.h"

#define STRIVEFRAMEDATA_API __declspec(dllexport)

class UREDGameCommon : public Unreal::UObject {};
UREDGameCommon* GameCommon;

typedef int(*GetGameMode_Func)(UREDGameCommon*);
GetGameMode_Func GetGameMode;

enum GAME_MODE : int32_t
{
    GAME_MODE_DEBUG_BATTLE = 0x0,
    GAME_MODE_ADVERTISE = 0x1,
    GAME_MODE_MAINTENANCEVS = 0x2,
    GAME_MODE_ARCADE = 0x3,
    GAME_MODE_MOM = 0x4,
    GAME_MODE_SPARRING = 0x5,
    GAME_MODE_VERSUS = 0x6,
    GAME_MODE_VERSUS_PREINSTALL = 0x7,
    GAME_MODE_TRAINING = 0x8,
    GAME_MODE_TOURNAMENT = 0x9,
    GAME_MODE_RANNYU_VERSUS = 0xA,
    GAME_MODE_EVENT = 0xB,
    GAME_MODE_SURVIVAL = 0xC,
    GAME_MODE_STORY = 0xD,
    GAME_MODE_MAINMENU = 0xE,
    GAME_MODE_TUTORIAL = 0xF,
    GAME_MODE_LOBBYTUTORIAL = 0x10,
    GAME_MODE_CHALLENGE = 0x11,
    GAME_MODE_KENTEI = 0x12,
    GAME_MODE_MISSION = 0x13,
    GAME_MODE_GALLERY = 0x14,
    GAME_MODE_LIBRARY = 0x15,
    GAME_MODE_NETWORK = 0x16,
    GAME_MODE_REPLAY = 0x17,
    GAME_MODE_LOBBYSUB = 0x18,
    GAME_MODE_MAINMENU_QUICK_BATTLE = 0x19,
    GAME_MODE_UNDECIDED = 0x1A,
    GAME_MODE_INVALID = 0x1B,
};

bool ShouldUpdateBattle = true;
bool ShouldAdvanceBattle = false;

struct UpdateAdvantage
{
    UpdateAdvantage()
    {
        Text = Unreal::FString(STR(""));
    }

    Unreal::FString Text;
};


/*
	Imitates FKey and bool return value
		FName
		TSharedPtr<FKeyDetails,...>
			FKeyDetails*
			FSharedReferencer<...>
				FReferenceControllerBase*
	Flattened:
		FName
		FKeyDetails*
		FReferenceControllerBase*
		bool
	Size matches 0x18 from ghidra, allignment matches as well
*/
struct InputParamData {
	Unreal::FName KeyName;
	void* KeyDetailsObject;
	void* KeyDetailsRefController;
	bool was_pressed;

	InputParamData(const Unreal::FName& src)
		: KeyName(src)
		, KeyDetailsObject(nullptr)
		, KeyDetailsRefController(nullptr)
		, was_pressed(false)
	{}
};

struct FLinearColor {
	float R, G, B, A;
};

struct DrawRectParams {
	FLinearColor RectColor;
	float ScreenX;
	float ScreenY;
	float ScreenW;
	float ScreenH;
};

struct GetSizeParams {
	int32_t SizeX = 0;
	int32_t SizeY = 0;
};

static const wchar_t* RawButtonNames[] = {
	L"Gamepad_LeftX",
	L"Gamepad_LeftY",
	L"Gamepad_RightX",
	L"Gamepad_RightY",
	L"Gamepad_LeftTriggerAxis",
	L"Gamepad_RightTriggerAxis",
	L"Gamepad_LeftThumbstick",
	L"Gamepad_RightThumbstick",
	L"Gamepad_Special_Left",
	L"Gamepad_Special_Right",
	L"Gamepad_FaceButton_Bottom",
	L"Gamepad_FaceButton_Right",
	L"Gamepad_FaceButton_Left",
	L"Gamepad_FaceButton_Top",
	L"Gamepad_LeftShoulder",
	L"Gamepad_RightShoulder",
	L"Gamepad_LeftTrigger",
	L"Gamepad_RightTrigger",
	L"Gamepad_DPad_Up",
	L"Gamepad_DPad_Down",
	L"Gamepad_DPad_Right",
	L"Gamepad_DPad_Left",
	L"Gamepad_LeftStick_Up,"
	L"Gamepad_LeftStick_Down",
	L"Gamepad_LeftStick_Right",
	L"Gamepad_LeftStick_Left",
	L"Gamepad_RightStick_Up",
	L"Gamepad_RightStick_Down",
	L"Gamepad_RightStick_Right",
	L"Gamepad_RightStick_Left"
};
constexpr int ButtonCount = sizeof(RawButtonNames) / sizeof(wchar_t*);
InputParamData* ButtonData[ButtonCount];
std::vector<bool> ButtonStates;

GetSizeParams SizeData;

std::vector<Unreal::UObject*> mod_actors{};
Unreal::UObject* input_actor;
Unreal::UObject* hud_actor;
Unreal::UObject* player_actor;

int debug_draw_offset = 0;

Unreal::UFunction* bp_function = nullptr;
Unreal::UFunction* press_function = nullptr;
Unreal::UFunction* drawrect_func = nullptr;
Unreal::UFunction* getplayer_func = nullptr;
Unreal::UFunction* getsize_func = nullptr;

bool MatchStartFlag = false;

void(*MatchStart_Orig)(AREDGameState_Battle*);
void MatchStart_New(AREDGameState_Battle* GameState)
{
    MatchStartFlag = true;
    ShouldAdvanceBattle = false;
    ShouldUpdateBattle = true;
    MatchStart_Orig(GameState);
}

int p1_advantage = 0;
int p2_advantage = 0;
int p1_hitstop_prev = 0;
int p2_hitstop_prev = 0;
int p1_act = 0;
int p2_act = 0;

UE4SSProgram* Program;

bool f2_pressed = false;
bool f3_pressed = false;

void(*UpdateBattle_Orig)(AREDGameState_Battle*, float);
void UpdateBattle_New(AREDGameState_Battle* GameState, float DeltaTime) {
	if (!GameCommon)
	{
		GameCommon = reinterpret_cast<UREDGameCommon*>(UObjectGlobals::FindFirstOf(FName(STR("REDGameCommon"))));
		return;
	}
    if (GetGameMode(GameCommon) != static_cast<int>(GAME_MODE_TRAINING))
    {
        UpdateBattle_Orig(GameState, DeltaTime);
        return;
    }
	
	if (GetAsyncKeyState(VK_F2) & 0x8000)
	{
		if (!f2_pressed)
		{
			ShouldUpdateBattle = !ShouldUpdateBattle;
			f2_pressed = true;
		}
	}
	else
	{
		f2_pressed = false;
	}
	if (GetAsyncKeyState(VK_F3) & 0x8000)
	{
		if (!f3_pressed)
		{
			ShouldAdvanceBattle = true;
			f3_pressed = true;
		}
	}
	else
	{
		f3_pressed = false;
	}

    if (ShouldUpdateBattle || ShouldAdvanceBattle)
	{
		UpdateBattle_Orig(GameState, DeltaTime);
		ShouldAdvanceBattle = false;

	    const auto engine = asw_engine::get();
	    if (!engine) return;

		asw_player* player_one = engine->players[0].entity;
		asw_player* player_two = engine->players[1].entity;

	    if ((player_one->action_time == 1 && player_one->hitstop != p1_hitstop_prev - 1)
	        || (player_two->action_time == 1 && player_two->hitstop != p2_hitstop_prev - 1))
	    {
	        if (!player_one->can_act() || !player_two->can_act())
	        {
	            if (!player_two->is_knockdown() || player_two->is_down_bound() || player_two->is_quick_down_1())
	            {
	                p1_advantage = player_one->calc_advantage() + p1_act;
	            }
	            if (!player_one->is_knockdown() || player_one->is_down_bound() || player_one->is_quick_down_1())
	            {
	                p2_advantage = player_two->calc_advantage() + p2_act;
	            }
	        }
	        else
	        {
	            p1_advantage = p1_act - p2_act;
	            p2_advantage = p2_act - p1_act;
	        }
	    }

	    if (player_one->is_stunned() && !player_two->is_stunned())
	        p1_advantage = -p2_advantage;
	    else if (player_two->is_stunned() && !player_one->is_stunned())
	        p2_advantage = -p1_advantage;
	    
	    p1_hitstop_prev = player_one->hitstop;
	    p2_hitstop_prev = player_two->hitstop;
	    if (player_one->can_act() && !player_two->can_act())
	        p1_act++;
	    else p1_act = 0;
	    if (player_two->can_act() && !player_one->can_act())
	        p2_act++;
	    else p2_act = 0;

        if (MatchStartFlag)
        {
			MatchStartFlag = false;

			/* battle state setup */
            static auto battle_trainingdamage_name = Unreal::FName(STR("Battle_TrainingDamage_C"), Unreal::FNAME_Add);

            UObjectGlobals::FindAllOf(battle_trainingdamage_name, mod_actors);
        	
            bp_function = mod_actors[0]->GetFunctionByNameInChain(STR("UpdateAdvantage"));
            
			/* input state setup */
			static auto input_class_name = Unreal::FName(STR("REDPlayerController_Battle"), Unreal::FNAME_Add);
			static auto input_func_name = Unreal::FName(STR("WasInputKeyJustPressed"), Unreal::FNAME_Add);

			input_actor = UObjectGlobals::FindFirstOf(input_class_name);

			if (input_actor) {
				RC::Output::send<LogLevel::Warning>(STR("Found Input Object\n"));
				press_function = input_actor->GetFunctionByNameInChain(input_func_name);
			}
			if (press_function) {
				RC::Output::send<LogLevel::Warning>(STR("Found Input Function\n"));
				ButtonStates.resize(ButtonCount, false);
				for (int idx = 0; idx < ButtonCount; ++idx) {
					ButtonData[idx] = new InputParamData(Unreal::FName(RawButtonNames[idx], Unreal::FNAME_Add));
				}
			}

			/* HUD setup */
			static auto hud_class_name = Unreal::FName(STR("REDHUD_Battle"), Unreal::FNAME_Add);
			static auto hud_drawrect_func_name = Unreal::FName(STR("DrawRect"), Unreal::FNAME_Add);
			static auto hud_getplayer_func_name = Unreal::FName(STR("GetOwningPlayerController"), Unreal::FNAME_Add);
			static auto player_getsize_func_name = Unreal::FName(STR("GetViewportSize"), Unreal::FNAME_Add);

			hud_actor = UObjectGlobals::FindFirstOf(hud_class_name);
			if (hud_actor) {
				RC::Output::send<LogLevel::Warning>(STR("Found HUD Object\n")); 
				drawrect_func = hud_actor->GetFunctionByNameInChain(hud_drawrect_func_name);
				getplayer_func = hud_actor->GetFunctionByNameInChain(hud_getplayer_func_name);
			}
			if (drawrect_func) {
				RC::Output::send<LogLevel::Warning>(STR("Found HUD drawRect Function\n"));
				/*for (auto* prop : drawrect_func->ForEachProperty()) {
					RC::Output::send<LogLevel::Warning>(STR(" - {}: {}\n"), prop->GetName(),prop->GetClass().GetName());
				}*/
			}
			if (getplayer_func) {
				RC::Output::send<LogLevel::Warning>(STR("Found HUD getPlayer Function\n"));
				hud_actor->ProcessEvent(getplayer_func, &player_actor);
			}
			if (player_actor) {
				RC::Output::send<LogLevel::Warning>(STR("Found Player Object\n"));
				getsize_func = player_actor->GetFunctionByNameInChain(player_getsize_func_name);
			}
			if (getsize_func) {
				RC::Output::send<LogLevel::Warning>(STR("Found Player getSize Function\n"));
				player_actor->ProcessEvent(getsize_func, &SizeData);

				RC::Output::send<LogLevel::Warning>(STR("VIEW SIZE - x:{}, y:{}\n"), SizeData.SizeX, SizeData.SizeY);
			}



        }
    	
    	if (mod_actors.empty()) return;

		/* Get input state */
		if (press_function) {
			for (int idx = 0; idx < ButtonCount; ++idx) {
				InputParamData& queried_key = *ButtonData[idx];
				input_actor->ProcessEvent(press_function, &queried_key);
				ButtonStates[idx] = queried_key.was_pressed;
				if (queried_key.was_pressed) {
					RC::Output::send<LogLevel::Warning>(STR("{} Key Was Pressed\n"), RawButtonNames[idx]);
				}
			}
		}

		/* Send arcsys state and input state to remote server */
		//SendFrameData(1, player_one);
		//SendFrameData(2, player_two);
		//SendInputData(ButtonStates);

	    UpdateAdvantage params = UpdateAdvantage();
	    auto p1_string = std::to_wstring(p1_advantage);
	    if (p1_advantage > 0)
	    {
	        p1_string.insert(0, STR("+"));
	    }
	    if (p1_advantage > 1500 || p1_advantage < -1500)
	    {
	        p1_string = STR("???");
	    }
	    params.Text = FString(p1_string.c_str());
	    mod_actors[mod_actors.size() - 1]->ProcessEvent(bp_function, &params);

	    auto p2_string = std::to_wstring(p2_advantage);
	    if (p2_advantage > 0)
	    {
	        p2_string.insert(0, STR("+"));
	    }
	    if (p2_advantage > 1500 || p2_advantage < -1500)
	    {
	        p2_string = STR("???");
	    }
	    UpdateAdvantage params2 = UpdateAdvantage();
	    params2.Text = FString(p2_string.c_str());
	    mod_actors[mod_actors.size() - 2]->ProcessEvent(bp_function, &params2);
	}
}

const void* vtable_hook(const void** vtable, const int index, const void* hook)
{
	DWORD old_protect;
	VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old_protect);
	const auto* orig = vtable[index];
	vtable[index] = hook;
	VirtualProtect(&vtable[index], sizeof(void*), old_protect, &old_protect);
	return orig;
}

using AHUD_PostRender_t = void(*)(void*);
AHUD_PostRender_t orig_AHUD_PostRender;



void hook_AHUD_PostRender(void* hud) {
	orig_AHUD_PostRender(hud);
	//void* offset = (void*)(((unsigned char*)hud) + 632);
	//void* value = *((void**)offset);

	//RC::Output::send<LogLevel::Warning>(STR("Canvas Prop: {} {} {}\n"), hud, offset, value);
	if (drawrect_func) {
		//RC::Output::send<LogLevel::Warning>(STR("Drawing {} {}\n"), hud, (void*)hud_actor);
		//auto* canvas_prop = hud_actor->GetPropertyByNameInChain(STR("Canvas"));
		//if (canvas_prop) {
			//canvas_prop->GetClass().GetName()
			
		//}

		debug_draw_offset += 1;
		if (debug_draw_offset > 60) {
			FLinearColor color_red{ 1.f,0.f,0.f,1.f };
			FLinearColor color_grn{ 0.f,1.f,0.f,1.f };
			FLinearColor color_blu{ 0.f,0.f,1.f,1.f };
			DrawRectParams func_params_red{ color_red, 0.f, 0, 200.f, 200.f };
			DrawRectParams func_params_grn{ color_grn, 0, 200, 200.f, 200.f };
			DrawRectParams func_params_blu{ color_blu, 200.f, 0.f, 200.f, 200.f };

			hud_actor->ProcessEvent(drawrect_func, &func_params_red);
			hud_actor->ProcessEvent(drawrect_func, &func_params_grn);
			hud_actor->ProcessEvent(drawrect_func, &func_params_blu);

			if (debug_draw_offset >= 120) {
				debug_draw_offset = 0;
			}
		}
	}
	
}

class StriveFrameData : public CppUserModBase
{
public:
	PLH::x64Detour* UpdateBattle_Detour;
	PLH::x64Detour* MatchStart_Detour;

    StriveFrameData() : CppUserModBase()
    {
    	ModName = STR("Strive Frame Advantage");
    	ModVersion = STR("1.0");
    	ModDescription = STR("A tool to display frame advantage.");
    	ModAuthors = STR("WistfulHopes");
    	UpdateBattle_Detour = nullptr;
    	MatchStart_Detour = nullptr;
    	// Do not change this unless you want to target a UE4SS version
    	// other than the one you're currently building with somehow.
    	//ModIntendedSDKVersion = STR("2.6");
    }

    ~StriveFrameData() override
    {
    }

    auto on_update() -> void override
    {
    }

    auto on_unreal_init() -> void override
    {
    	GameCommon = reinterpret_cast<UREDGameCommon*>(UObjectGlobals::FindFirstOf(FName(STR("REDGameCommon"))));

    	Program = &UE4SSProgram::get_program();

	    const uint64_t UpdateBattle_Addr = sigscan::get().scan("\x40\x53\x57\x41\x54\x41\x55\x48\x81\xEC\x88\x00\x00\x00", "xxxxxxxxxxxxxx");
    	UpdateBattle_Detour = new PLH::x64Detour(
			UpdateBattle_Addr, reinterpret_cast<uint64_t>(&UpdateBattle_New),
			reinterpret_cast<uint64_t*>(&UpdateBattle_Orig));
    	UpdateBattle_Detour->hook();

	    const uint64_t MatchStart_Addr = sigscan::get().scan("\x44\x39\x68\x08\x74\x00\x48\x8B\x18\xEB\x00\x48\x8B\xDF", "xxxxx?xxxx?xxx") - 0x8D;
    	MatchStart_Detour = new PLH::x64Detour(
			MatchStart_Addr, reinterpret_cast<uint64_t>(&MatchStart_New),
			reinterpret_cast<uint64_t*>(&MatchStart_Orig));
    	MatchStart_Detour->hook();
		
	    const uintptr_t GetGameMode_Addr = sigscan::get().scan("\x0F\xB6\x81\xF0\x02\x00\x00\xC3", "xxxxxxxx");
    	GetGameMode = reinterpret_cast<GetGameMode_Func>(GetGameMode_Addr);

		const auto** AHUD_vtable = (const void**)get_rip_relative(sigscan::get().scan("\x48\x8D\x05\x00\x00\x00\x00\xC6\x83\x18\x03", "xxx????xxxx") + 3);
		orig_AHUD_PostRender = (AHUD_PostRender_t)vtable_hook(AHUD_vtable, 214, hook_AHUD_PostRender);
    	
    	ASWInitFunctions();
    	bbscript::BBSInitializeFunctions();
    }
};

extern "C"
{
    STRIVEFRAMEDATA_API CppUserModBase* start_mod()
    {
        return new StriveFrameData();
    }

    STRIVEFRAMEDATA_API void uninstall_mod(CppUserModBase* mod)
    {
        delete mod;
    }
}