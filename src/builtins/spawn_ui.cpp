// The spawn built-in's menu tab: Vehicles (the ship list, boarding options and the current ship).
// Moved from menu.cpp unchanged; registered through sco.ui by spawn_plugin.cpp and drawn by the
// menu shell.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>
#include <cstring>

void DrawVehiclesTab(void*, void*) {
    static int  selected = 0;
    static char filter[64] = "";
    static MenuSpawnOptions opt;

    SectionHeading("生成飞船");
    const int count = Menu_ShipCount();
    if (count < 0) {
        Hint("正在加载飞船列表（需要先进入游戏世界）...");
    } else if (count == 0) {
        Hint("没有找到飞船，请检查 data\\ships.txt。");
    } else {
        const MenuShip* ships = Menu_Ships();
        if (selected >= count) selected = 0;
        if (SearchBox("##shipFilter", "搜索飞船（中文名或代号）", filter, sizeof(filter)))
            for (int i = 0; i < count; ++i)
                if (MatchesFilter(ships[i].name, filter)) { selected = i; break; }
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter
                                    | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##ships", 3, flags, ImVec2(0, 230))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("飞船", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("尺寸", ImGuiTableColumnFlags_WidthFixed, 44);
            ImGui::TableSetupColumn("长度", ImGuiTableColumnFlags_WidthFixed, 64);
            ImGui::TableHeadersRow();
            for (int i = 0; i < count; ++i) {
                if (!MatchesFilter(ships[i].name, filter)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char label[96];
                snprintf(label, sizeof(label), "%s##s%d", ships[i].name, i);
                if (ImGui::Selectable(label, i == selected, ImGuiSelectableFlags_SpanAllColumns)) selected = i;
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", ships[i].size);
                ImGui::TableNextColumn();
                if (ships[i].length > 0) ImGui::TextDisabled("%.0f 米", ships[i].length);
            }
            ImGui::EndTable();
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##height", &opt.height, 0.0f, 500.0f, "在你上方 %.0f 米处生成");
        static const char* const kBoard[] = { "不登船", "登船并坐上驾驶座", "按名称选择座位登船", "生成后再选座位" };
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##board", &opt.seatMode, kBoard, 4);
        if (opt.seatMode == SeatMode_Named) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##seatName", "座位名称，例如 copilot 或 turret left", opt.seatName, sizeof(opt.seatName));
            ImGui::SetItemTooltip("每个词都必须出现在座位名称里。“船员”标签页可以查看飞船的座位名称。");
        }
        const bool boarding = opt.seatMode == SeatMode_Pilot || opt.seatMode == SeatMode_Named;
        ImGui::BeginDisabled(!boarding);
        ImGui::Checkbox("移除我座位上的 NPC", &opt.replaceNpc);
        ImGui::SameLine(0, 24);
        ImGui::Checkbox("启动电源", &opt.flightReady);
        ImGui::SetItemTooltip("坐上驾驶座后自动启动飞船电源。");
        ImGui::EndDisabled();

        char spawn[96];
        snprintf(spawn, sizeof(spawn), "生成 %s", ships[selected].name);
        if (PrimaryButton(spawn)) {
            MenuSpawnOptions send = opt;
            if (!boarding) send.flightReady = false;
            Menu_RequestSpawn(selected, send);
            if (opt.seatMode != SeatMode_PickLater) MenuClose();
        }
    }

    SectionHeading("当前飞船");
    static bool shipAmmo = false;
    if (ImGui::Checkbox("飞船无限弹药", &shipAmmo)) InvokeCreativeToggle("creative.ship_ammo", shipAmmo, &shipAmmo);
    ImGui::SetItemTooltip("为你所在的飞船补满弹药。需要 creative 插件（开启方法见“玩家”页）。");
    if (ImGui::Button("电源开 / 关", ImVec2(-1, 0))) Menu_RequestFlightReady();
    Hint("切换“船员”标签页中飞船的飞行就绪状态，相当于在驾驶座按 R。");
}
