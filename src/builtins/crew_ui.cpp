// The crew built-in's menu tab: Crew (the target ship's seats and its NPC crew). Moved from
// menu.cpp unchanged; registered through sco.ui by crew_plugin.cpp and drawn by the menu shell.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../spawner.h"
#include "../teleport.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>
#include <cstring>

// "VNCL_Mauler_Gunner_Seat_200006242347" on a VNCL_Mauler -> "Gunner Seat 3"
static void PrettySeatNames(const MenuSeat* seats, int n, const char* ship, char (*out)[64]) {
    static char base[128][64];
    for (int i = 0; i < n; ++i) {
        char name[64];
        strcpy_s(name, seats[i].name);
        if (char* cut = strrchr(name, '_'); cut && cut[1] && strspn(cut + 1, "0123456789") == strlen(cut + 1)) *cut = 0;
        const char* a = name;          // drop the leading words the seat shares with the ship's name
        const char* b = ship;
        for (;;) {
            const size_t la = strcspn(a, "_"), lb = strcspn(b, "_ ");
            if (!la || la != lb || _strnicmp(a, b, la) != 0 || !a[la]) break;
            a += la + 1;
            b += lb + (b[lb] ? 1 : 0);
        }
        strcpy_s(base[i], a);
        for (char* c = base[i]; *c; ++c) if (*c == '_') *c = ' ';
    }
    for (int i = 0; i < n; ++i) {      // number repeats: Gunner Seat 1, Gunner Seat 2, ...
        int total = 0, before = 0;
        for (int j = 0; j < n; ++j)
            if (_stricmp(base[i], base[j]) == 0) { ++total; if (j < i) ++before; }
        if (total > 1) snprintf(out[i], 64, "%s %d", base[i], before + 1);
        else strcpy_s(out[i], 64, base[i]);
    }
}

// Run by the crew built-in's tick: the tab's buttons queue spawner.cpp's seat actions, which this carries out.
void CrewTabTick(uint32_t now) {
    if (g_tp.ok) ProcessCrew(now);
}

void DrawCrewTab(void*, void*) {
    if (!Menu_SeatControlAvailable()) {
        SectionHeading("船员");
        Hint("当前游戏版本不支持座位控制，详情见 mod.log。");
        return;
    }
    static MenuSeat seats[128];
    static char pretty[128][64];
    static unsigned long long selectedSeat = 0;
    static bool replace = true;
    char ship[64] = "";
    const int count = Menu_GetSeats(seats, 128, ship, sizeof(ship));

    SectionHeading("飞船");
    ImGui::TextUnformatted(count < 0 ? "未选择飞船" : ship);
    ImGui::SameLine();
    const float button = 190;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - button);
    if (ImGui::Button("使用我所在的飞船", ImVec2(button, 0))) Menu_TargetShipImIn();
    if (count < 0) { Hint("先生成一艘飞船，或登上一艘后点“使用我所在的飞船”。"); return; }
    if (count == 0) { Hint("正在等待飞船加载..."); return; }

    SectionHeading("座位");
    PrettySeatNames(seats, count, ship, pretty);
    int sel = -1;
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter
                                | ImGuiTableFlags_BordersInnerH;
    if (ImGui::BeginTable("##seats", 2, flags, ImVec2(0, 250))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("座位", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("乘员", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableHeadersRow();
        for (int i = 0; i < count; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char label[96];
            snprintf(label, sizeof(label), "%s##seat%llu", pretty[i], seats[i].id);
            if (ImGui::Selectable(label, seats[i].id == selectedSeat, ImGuiSelectableFlags_SpanAllColumns)) selectedSeat = seats[i].id;
            ImGui::SetItemTooltip("%s", seats[i].name);
            if (seats[i].id == selectedSeat) sel = i;
            ImGui::TableNextColumn();
            switch (seats[i].state) {
            case SeatState_You:   ImGui::TextColored(kLeaf, "你"); break;
            case SeatState_Npc:   ImGui::TextUnformatted("NPC"); break;
            case SeatState_Taken: ImGui::TextDisabled("未知"); break;
            default:              ImGui::TextDisabled("空"); break;
            }
        }
        ImGui::EndTable();
    }

    const int state = sel >= 0 ? seats[sel].state : -1;
    const float quarter = Columns(4);
    ImGui::BeginDisabled(sel < 0 || state == SeatState_You);
    if (ImGui::Button("坐这里", ImVec2(quarter, 0))) Menu_RequestSit(seats[sel].id, replace);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state != SeatState_Npc && state != SeatState_You);
    if (ImGui::Button("起身", ImVec2(quarter, 0))) Menu_RequestStandUp(seats[sel].id);
    ImGui::SetItemTooltip("让座位上的人起身，但留在船上。");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state != SeatState_Npc);
    if (ImGui::Button("移除 NPC", ImVec2(quarter, 0))) Menu_RequestKick(seats[sel].id);
    ImGui::SetItemTooltip("把这个 NPC 从游戏中移除。");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state != SeatState_Empty);
    if (ImGui::Button("添加 NPC", ImVec2(quarter, 0))) Menu_RequestAddCrew(seats[sel].id, g_npcPick);
    ImGui::EndDisabled();
    ImGui::Checkbox("如果选中的座位上有 NPC，先移除它", &replace);

    SectionHeading("船员");
    Hint("要安排到座位上的 NPC：");
    const bool haveNpcs = NpcPicker();
    const float third = Columns(3);
    ImGui::BeginDisabled(!haveNpcs);
    if (ImGui::Button("填满空座位", ImVec2(third, 0))) Menu_RequestFillCrew(g_npcPick);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("所有 NPC 起身", ImVec2(third, 0))) Menu_RequestStandAll();
    ImGui::SameLine();
    if (ImGui::Button("移除所有 NPC", ImVec2(third, 0))) Menu_RequestClearCrew();
    Hint("添加的 NPC 只会坐在座位上，暂时不会驾驶飞船或操作炮塔。");
}
