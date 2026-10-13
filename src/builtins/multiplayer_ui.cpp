// The multiplayer built-in's menu tab: host, join or leave a session, and who's in it. Registered
// through sco.ui by multiplayer_plugin.cpp and drawn by the menu shell.
#include "multiplayer.h"
#include "tabs.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>

namespace {

char g_message[192] = "";   // the last Host / Join answer

void Answer(const char* err, const char* ok) { snprintf(g_message, sizeof(g_message), "%s", err ? err : ok); }

void SessionForm(MpSettings& s) {
    SectionHeading("你");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##mpName", "你的名字（其他玩家看到的）", s.name, sizeof(s.name));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##mpPass", "会话口令（至少 8 个字符）", s.passphrase, sizeof(s.passphrase),
                             ImGuiInputTextFlags_Password);
    ImGui::Checkbox("在这台电脑上记住口令", &s.rememberPass);
    ImGui::SetItemTooltip("以明文保存在 data\\storage\\multiplayer.db 里。不勾选则每次都要输入。");

    SectionHeading("主持会话");
    ImGui::SetNextItemWidth(Columns(2));
    ImGui::InputInt("UDP 端口##mpHostPort", &s.hostPort, 0);
    if (PrimaryButton("主持")) Answer(Mp_Host(), "正在主持：把你的局域网地址、端口和口令告诉其他玩家。");

    SectionHeading("加入会话");
    ImGui::SetNextItemWidth(Columns(2));
    ImGui::InputTextWithHint("##mpAddr", "主持者的地址（例如 192.168.1.20）", s.joinAddress, sizeof(s.joinAddress));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputInt("##mpJoinPort", &s.joinPort, 0);
    if (PrimaryButton("加入")) Answer(Mp_Join(), "正在加入...");
}

void PeerList() {
    const int n = Mp_PeerCount();
    if (!ImGui::BeginTable("##mpPeers", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("玩家");
    ImGui::TableSetupColumn("显示为");
    ImGui::TableSetupColumn("飞船");
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int i = 0; i < n; ++i) {
        MpPeerView v;
        if (!Mp_Peer(i, v)) continue;
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", v.name);
        ImGui::SetItemTooltip("玩家 %llu，会话 ID %016llx", static_cast<unsigned long long>(v.peer),
                              static_cast<unsigned long long>(v.sessionId));
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", v.avatar);
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", v.ship[0] ? v.ship : "-");
        ImGui::TableNextColumn();
        if (!v.self && ImGui::SmallButton("前往")) Answer(Mp_GoTo(v.peer), "已传送。");
        ImGui::PopID();
    }
    ImGui::EndTable();
}

}  // namespace

void DrawMultiplayerTab(void*, void*) {
    if (!Mp_Enabled()) {
        Hint("多人功能不可用（sc-offline.ini 里 multiplayer = off，或环境变量 SC_OFFLINE_MULTIPLAYER=off，或缺少 sco.net；详见 mod.log）。");
        return;
    }
    MpSettings& s = Mp_Settings();
    SectionHeading("会话");
    ImGui::Text("%s", Mp_StateText());
    if (Mp_LastReason()[0]) ImGui::TextColored(kMuted, "上次会话结束原因：%s", Mp_LastReason());
    if (g_message[0]) ImGui::TextWrapped("%s", g_message);
    if (Mp_InSession()) {
        if (ImGui::Button("离开", ImVec2(-1, 0))) { Mp_Leave(); g_message[0] = 0; }
        PeerList();
        if (!Mp_GhostsReady()) Hint("替身已关闭：当前游戏版本缺少 spawn.entities 1.2 或 teleport.spatial。");
    } else {
        SessionForm(s);
    }

    SectionHeading("其他玩家的替身");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##mpAvatar", "其他玩家看到的你的 NPC 类名", s.avatarClass, sizeof(s.avatarClass));
    ImGui::SetItemTooltip("npcs.txt 里的一个人类类名。对方的 npcs.txt 里也有它时，对方看到的你就是它，\n"
                          "否则是对方自己选的类名。");
    ImGui::Checkbox("显示其他玩家的飞船", &s.showShips);
    if (ImGui::Button("保存设置", ImVec2(-1, 0))) Mp_SaveSettings();
    Hint("仅限局域网（用 VPN 时需在 sc-offline.ini 的 multiplayer_allow 里放行）。消息用口令签名，但"
         "不加密。替身只是外观：没有共享的物理、伤害、任务或物品。");
    if (Mp_AllowText()[0]) ImGui::TextColored(kMuted, "另外允许：%s", Mp_AllowText());
}
