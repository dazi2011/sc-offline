// The loadout built-in's menu tabs: Player (noclip, god mode, infinite ammo and the gear menu) and
// Squadron 42 (outfits, the SQ42 settings, the [sq42] spawn list, SQ42 ships and the console).
// Moved from menu.cpp; registered through sco.ui by loadout_plugin.cpp and drawn by the menu shell
// on the game thread. The Player tab's noclip, god mode and infinite ammo are the optional creative
// plugin's (data/plugins/creative): the checkboxes run its commands through sco_api's invoke.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include "../build.h"
#include "../cvars.h"
#include "../spawner.h"
#include <cstdio>
#include <cstring>

static const sco_api* g_api = nullptr;
static sco_plugin*    g_self = nullptr;

void RegisterPlayerTab(const sco_api* api, sco_plugin* self) {
    g_api = api;
    g_self = self;
    RegisterBuiltinTab(api, self, "loadout.player", "玩家", kTabPlayer, DrawPlayerTab);
}

// The command's reply in the status strip (which also logs it). A refusal puts the checkbox back; a
// missing command means the creative plugin isn't loaded. SCO_FAILED (the command ran and failed)
// shows the command's own reply; SCO_UNAVAILABLE means the capability isn't on this build.
static void CreativeReply(sco_result r, const char* reply, void* ctx) {
    if (r == SCO_OK) {
        SetMenuStatus("%s", reply ? reply : "");
        return;
    }
    if (ctx) *static_cast<bool*>(ctx) = !*static_cast<bool*>(ctx);
    if (reply && reply[0]) SetMenuStatus("%s", reply);
    else if (r == SCO_NOT_FOUND) SetMenuStatus("需要 creative 插件：删除 data\\plugins\\creative 里名为 disabled 的文件，确认插件总开关已打开（SC_OFFLINE_PLUGINS=on），然后重启游戏。");
    else if (r == SCO_UNAVAILABLE) SetMenuStatus("当前游戏版本不支持。");
    else SetMenuStatus("creative 插件拒绝了请求（结果 %d）。", static_cast<int>(r));
}

void InvokeCreativeToggle(const char* command, bool on, bool* flag) {
    if (!g_api || !g_self) { if (flag) *flag = !on; return; }
    sco_arg arg = {};
    arg.type = SCO_ARG_BOOL;
    arg.v.i = on ? 1 : 0;
    g_api->invoke(g_self, command, &arg, 1, CreativeReply, flag);
}

void InvokeCreativeSpeed(float metresPerSecond) {
    if (!g_api || !g_self) return;
    sco_arg arg = {};
    arg.type = SCO_ARG_FLOAT;
    arg.v.f = metresPerSecond;
    g_api->invoke(g_self, "creative.noclip_speed", &arg, 1, CreativeReply, nullptr);
}

static void GearCombo(int slot, const char* label, const char* none, int& pick, const char* filter, float width) {
    const int n = Menu_GearCount(slot);
    if (pick >= n) pick = -1;
    char preview[112], id[32];
    snprintf(preview, sizeof(preview), "%s：%s", label, pick >= 0 ? Menu_GearName(slot, pick) : none);
    snprintf(id, sizeof(id), "##gear%d", slot);
    ImGui::SetNextItemWidth(width);
    if (!ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge)) return;
    if (ImGui::Selectable(none, pick < 0)) pick = -1;
    for (int i = 0; i < n; ++i) {
        const char* name = Menu_GearName(slot, i);
        if (!MatchesFilter(name, filter)) continue;
        ImGui::PushID(i);
        if (ImGui::Selectable(name, i == pick)) pick = i;
        if (i == pick) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }
    ImGui::EndCombo();
}

void DrawPlayerTab(void*, void*) {
    SectionHeading("移动");
    static bool  noclip = false;
    static float speed = 30.0f;
    if (ImGui::Checkbox("穿墙飞行", &noclip)) {
        InvokeCreativeSpeed(speed);
        InvokeCreativeToggle("creative.noclip", noclip, &noclip);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##noclipSpeed", &speed, 1.0f, 500.0f, "速度 %.0f", ImGuiSliderFlags_Logarithmic))
        InvokeCreativeSpeed(speed);
    Hint("F7 记录当前位置，F8 传送回去。“传送”标签页里还有命名地点和各类目的地。");

    SectionHeading("保护");
    static bool god = false, ammo = false;
    if (ImGui::Checkbox("无敌模式", &god)) InvokeCreativeToggle("creative.god", god, &god);
    ImGui::SameLine(0, 24);
    if (ImGui::Checkbox("无限弹药", &ammo)) InvokeCreativeToggle("creative.ammo", ammo, &ammo);
    Hint("穿墙飞行、无敌模式和无限弹药由 creative 插件提供，本版默认开启（还需插件总开关：sc-offline.ini 的 plugins = on，或环境变量 SC_OFFLINE_PLUGINS=on）。想关掉它，在 data\\plugins\\creative 里放一个名为 disabled 的空文件，然后重启游戏。");

    SectionHeading("装备");
    static int  gear[Gear_SlotCount] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    static char gearFilter[64] = "";
    static const struct { const char* label; const char* none; } kGear[Gear_SlotCount] = {
        { "内衬服", "默认" }, { "头盔", "无" }, { "躯干护甲", "无" }, { "手臂护甲", "无" }, { "腿部护甲", "无" },
        { "背包", "无" }, { "主武器", "无" }, { "副武器", "无" }, { "弹药", "与武器匹配" }, { "手雷", "无" } };
    if (Menu_GearCount(0) < 0) { Hint("正在加载装备（需要先进入游戏世界）..."); return; }
    SearchBox("##gearFilter", "搜索装备", gearFilter, sizeof(gearFilter));
    const float half = Columns(2);
    for (int s = 0; s < Gear_SlotCount; ++s) {
        if (s % 2) ImGui::SameLine();
        GearCombo(s, kGear[s].label, kGear[s].none, gear[s], gearFilter, half);
    }
    if (PrimaryButton("穿戴装备")) { Menu_RequestEquip(gear); MenuClose(); }
}

void DrawSq42Tab(void*, void*) {
    static bool spoilerOk = false;
    if (!spoilerOk) {
        ImGui::SeparatorText("剧透警告");
        ImGui::TextWrapped("这个标签页可能含有《42 中队》的剧透。");
        ImGui::TextWrapped("点“确定”继续。");
        if (ImGui::Button("确定", ImVec2(120, 0))) spoilerOk = true;
        ImGui::SameLine();
        if (ImGui::Button("返回", ImVec2(120, 0))) MenuSelectFirstTab();
        return;
    }

    ImGui::SeparatorText("服装");
    const int outfits = Menu_OutfitCount();
    static int outfit = 0;
    if (outfits < 0) {
        ImGui::TextWrapped("正在加载服装...（需要先进入游戏世界）");
    } else if (outfits == 0) {
        ImGui::TextWrapped("没有找到服装，请检查 outfits.txt。");
    } else {
        static char filter[64] = "";
        if (outfit >= outfits) outfit = 0;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##outfitFilter", "搜索服装...", filter, sizeof(filter));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##outfit", Menu_OutfitName(outfit), ImGuiComboFlags_HeightLargest)) {
            for (int i = 0; i < outfits; ++i) {
                const char* name = Menu_OutfitName(i);
                if (!MatchesFilter(name, filter)) continue;
                ImGui::PushID(i);
                if (ImGui::Selectable(name, i == outfit)) outfit = i;
                if (i == outfit) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("穿上 SQ42 服装", ImVec2(-1, 0))) {
            Menu_RequestWearOutfit(outfit);
            MenuClose();
        }
    }
    static bool visor = false;
    if (ImGui::Checkbox("SQ42 面罩 HUD（下次穿戴装备或服装时生效）", &visor))
        Menu_SetS42VisorHud(visor);

    ImGui::SeparatorText("设置");
    for (int i = 0; i < Menu_S42SettingCount(); ++i) {
        bool on = Menu_S42SettingOn(i);
        ImGui::PushID(i);
        const bool known = Menu_S42SettingKnown(i);   // greyed until the game thread has read the cvar
        ImGui::BeginDisabled(!known);
        if (ImGui::Checkbox(Menu_S42SettingLabel(i), &on)) Menu_RequestS42Setting(i, on);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", known ? Menu_S42SettingTip(i) : "正在从游戏读取此设置...");
        ImGui::PopID();
    }

    ImGui::SeparatorText("生成");
    {
        static int   thing = -1;
        static char  thingFilter[64] = "";
        static bool  inFront = true;
        static float ahead = 8.0f;
        const int buildables = Menu_BuildCount();
        if (buildables < 0) {
            ImGui::TextWrapped("正在加载...（需要先进入游戏世界）");
        } else if (buildables == 0) {
            ImGui::TextWrapped("没有找到可建造物体，请检查 buildables.txt。");
        } else {
            if (thing < 0) {
                thing = 0;
                for (int i = 0; i < buildables; ++i)
                    if (_stricmp(Menu_BuildCategory(i), "sq42") == 0) { thing = i; break; }
            }
            if (thing >= buildables) thing = 0;
            ImGui::Checkbox("在你面前生成", &inFront);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##thingFilter", "搜索 [sq42] 分组...", thingFilter, sizeof(thingFilter));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##sq42thing", Menu_BuildName(thing), ImGuiComboFlags_HeightLargest)) {
                for (int i = 0; i < buildables; ++i) {
                    // Only the [sq42] group, like the original; the text filter narrows it further.
                    if (_stricmp(Menu_BuildCategory(i), "sq42") != 0) continue;
                    const char* name = Menu_BuildName(i);
                    if (!MatchesFilter(name, thingFilter)) continue;
                    ImGui::PushID(i);
                    if (ImGui::Selectable(name, i == thing)) thing = i;
                    if (i == thing) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (inFront) {
                ImGui::SetNextItemWidth(200);
                ImGui::SliderFloat("前方距离（米）", &ahead, 1.0f, 50.0f, "%.0f");
            }
            if (ImGui::Button("生成它", ImVec2(-1, 0))) {
                Menu_RequestPlace(thing, inFront, ahead);
                MenuClose();
            }
            ImGui::SetItemTooltip("“建造”页的“撤销上一步”和“清空基地”也会移除这些物体。");
        }
    }

    ImGui::SeparatorText("飞船");
    struct Entry { const char* label; const char* cls; bool enemyWing; float height; bool sit; };
    static const struct { const char* label; const char* cls; } kSq42Ships[] = {
        { "伊德里斯-P（斯坦顿号同级舰）", "AEGS_Idris_P" },
        { "角斗士（SQ42 战斗机）",        "AEGS_Gladius" },
        { "报复者（带 S42 HUD）",   "AEGS_Retaliator" },
        { "星际远航者（第 5、7、9 章）",        "MISC_Starfarer" },
        { "复仇者潜行者（S42 残骸）",   "AEGS_Avenger_Stalker" },
        { "大黄蜂（卡尔·梅森的座驾）",     "ANVL_Hornet_F7C" },
        { "范杜尔刀锋（AI）",            "VNCL_Blade_PU_AI_VAN" },
        { "范杜尔镰刀（AI）",           "VNCL_Scythe_PU_AI_VAN" },
        { "范杜尔长柄刀（AI）",           "VNCL_Glaive_PU_AI_VAN" },
        { "范杜尔毒刺（AI）",          "VNCL_Stinger_PU_AI_VAN" },
    };
    Entry list[16];
    static_assert(sizeof(kSq42Ships) / sizeof(kSq42Ships[0]) + 2 <= sizeof(list) / sizeof(list[0]),
                  "SQ42 ship table outgrew Entry list[]");
    int   n = 0;
    for (size_t i = 0; i < sizeof(kSq42Ships) / sizeof(kSq42Ships[0]); ++i)
        list[n++] = { kSq42Ships[i].label, kSq42Ships[i].cls, false, 0.0f, true };
    for (int i = 0; i < n; ++i)
        if (strncmp(list[i].cls, "VNCL_", 5) == 0) { list[i].height = 300.0f; list[i].sit = false; }

    // Not the original's dormant "[battle]" mode (two Bengals fighting each other): these
    // spawn one UEE Bengal, the second with a Vanduul wing when an enemy side was found.
    static char bengalWing[64];
    const bool enemySide = Menu_EnemySideAvailable();
    strcpy_s(bengalWing, enemySide ? "孟加拉 + 范杜尔编队" : "孟加拉 + 编队（UEE，未找到敌方阵营）");
    list[n++] = { "孟加拉（UEE）", "RSI_Bengal_PU_AI_UEE", false,      1500.0f, false };
    list[n++] = { bengalWing,     "RSI_Bengal_PU_AI_UEE", enemySide, 1500.0f, false };

    static int   pick = 0;
    static char  sqFilter[64] = "";
    static float height = 30.0f;   // the original's fixed spawn height for your own ships
    static bool  sit = true;
    if (pick >= n) pick = 0;
    // Greys out classes this game build doesn't have, like the original, instead of failing
    // after the click with "unknown entity class".
    auto known = [](const char* cls) {
        const int count = Menu_ShipCount();
        if (count < 0) return true;   // ship list still loading: don't block
        const MenuShip* ships = Menu_Ships();
        for (int i = 0; i < count; ++i)
            if (_stricmp(ships[i].name, cls) == 0) return true;
        return false;
    };

    ImGui::TextWrapped("己方飞船会让你坐上驾驶座；范杜尔飞船在上方 300 米生成并向你发起攻击。");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##sq42shipFilter", "搜索飞船...", sqFilter, sizeof(sqFilter));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##sq42ships", list[pick].label, ImGuiComboFlags_HeightLargest)) {
        for (int i = 0; i < n; ++i) {
            if (sqFilter[0] && !MatchesFilter(list[i].label, sqFilter)) continue;
            ImGui::PushID(i);
            const bool have = known(list[i].cls);
            if (ImGui::Selectable(list[i].label, i == pick, have ? 0 : ImGuiSelectableFlags_Disabled)) pick = i;
            if (!have) ImGui::SetItemTooltip("当前游戏版本的飞船列表里没有 %s。", list[i].cls);
            if (i == pick) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const bool pickKnown = known(list[pick].cls);
    if (list[pick].sit) {
        ImGui::SliderFloat("离我的高度（米）", &height, 0.0f, 500.0f, "%.0f");
        ImGui::Checkbox("让我坐上驾驶座", &sit);
    }
    ImGui::BeginDisabled(!pickKnown);
    const bool spawnClicked = ImGui::Button("生成", ImVec2(-1, 42));
    ImGui::EndDisabled();
    if (spawnClicked) {
        Menu_RequestSpawnClass(list[pick].cls,
                               list[pick].sit ? height : list[pick].height,
                               list[pick].sit && sit, list[pick].sit && sit,
                               list[pick].enemyWing);
        MenuClose();
    }

    ImGui::SeparatorText("控制台");
    static char cmd[256] = "";
    const bool consoleReady = Menu_ConsoleReady();
    ImGui::BeginDisabled(!consoleReady);
    const float runWidth = ImGui::CalcTextSize("执行").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(-(runWidth + ImGui::GetStyle().ItemSpacing.x));
    bool run = ImGui::InputTextWithHint("##console",
                                        "控制台命令，例如 i_target_selector.targeting2_enabled 1",
                                        cmd, sizeof(cmd), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    run |= ImGui::Button("执行");
    ImGui::EndDisabled();
    if (run && cmd[0]) {
        Menu_RunConsole(cmd);
        cmd[0] = 0;
    }
    if (!consoleReady) ImGui::TextDisabled("还没有找到游戏的控制台。");
    ImGui::TextWrapped("在游戏自带的控制台中执行，结果写在游戏日志里，不在这里显示。");
}
