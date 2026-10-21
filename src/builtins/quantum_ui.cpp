// The quantum built-in's menu tab: Travel (places, the scan and saved spots), with Chinese place
// names from data\place_names_zh.txt. Moved from menu.cpp; registered through sco.ui by
// quantum_plugin.cpp and drawn by the menu shell.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include "../travel.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <share.h>
#include <string>
#include <unordered_map>

static bool HasText(const char* filter) { return filter[strspn(filter, " _")] != 0; }

// Chinese names for systems and places, read once from data\place_names_zh.txt ("key|name", made by
// tools/zh-cn from the game's language pack). Keys are entity names, localization body keys
// (Stanton1a, Stanton1_L1) or "system:<name>". Only the tab shows them; travel uses the entity name.
static std::unordered_map<std::string, std::string> g_zhPlaces;
static bool g_zhPlacesLoaded = false;

static std::string LowerKey(const char* s) {
    std::string k(s);
    for (char& c : k) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return k;
}

static const char* ZhLookup(const std::string& key) {
    if (!g_zhPlacesLoaded) {
        g_zhPlacesLoaded = true;
        char path[MAX_PATH];
        if (DataFilePath(path, sizeof(path), "place_names_zh.txt"))
            if (FILE* f = _fsopen(path, "r", _SH_DENYNO)) {
                char line[512];
                while (fgets(line, sizeof(line), f)) {
                    line[strcspn(line, "\r\n")] = 0;
                    char* sep = strchr(line, '|');
                    if (line[0] == '#' || !sep || sep == line || !sep[1]) continue;
                    *sep = 0;
                    g_zhPlaces[LowerKey(line)] = sep + 1;
                }
                fclose(f);
            }
    }
    const auto it = g_zhPlaces.find(key);
    return it == g_zhPlaces.end() ? nullptr : it->second.c_str();
}

static const char* ZhSystem(const char* system) {
    const char* zh = ZhLookup("system:" + LowerKey(system));
    return zh ? zh : system;
}

static const char* ZhPlace(const char* name, const char* entity) {
    if (const char* zh = ZhLookup(LowerKey(entity))) return zh;
    if (_strnicmp(entity, "OOC_", 4) == 0) {
        // OOC_Stanton_1a_Ariel -> Stanton1a, OOC_Stanton1_L1 -> Stanton1_L1
        const char* a = entity + 4;
        const char* b = strchr(a, '_');
        if (b) {
            const char* c = strchr(b + 1, '_');
            const std::string first(a, b), second(b + 1, c ? c : b + 1 + strlen(b + 1));
            if (const char* zh = ZhLookup(LowerKey((first + second).c_str()))) return zh;
            if (const char* zh = ZhLookup(LowerKey((first + "_" + second).c_str()))) return zh;
        }
    }
    return name;
}

// Copies src into out, cut before a UTF-8 character that doesn't fit.
static void CopyUtf8(char* out, size_t n, const char* src) {
    size_t len = strlen(src);
    if (len >= n) {
        len = n - 1;
        while (len && (static_cast<unsigned char>(src[len]) & 0xC0) == 0x80) --len;
    }
    memcpy(out, src, len);
    out[len] = 0;
}

void DrawTravelTab(void*, void*) {
    static TravelPlace    places[3000];
    static int            order[3000];
    static TravelBookmark marks[400];
    static char  filter[64] = "";
    static char  markName[64] = "";
    static char  selected[96] = "";       // entity name of the selected place
    static float altitude = 2000.0f;
    static TravelLanding landing;            // where on planets and moons (travel.h)
    static bool  showMinor = false;

    static int np = 0, placesVersion = -1;
    char here[32];
    Travel_CurrentSystem(here, sizeof(here));
    const int version = Travel_PlacesVersion();
    const bool placesChanged = version != placesVersion;
    if (placesChanged) { placesVersion = version; np = Travel_GetPlaces(places, 3000); }
    const int nm = Travel_GetBookmarks(marks, 400);
    const bool searching = HasText(filter);

    // Systems that have anything in them; the one you're in first, then A-Z.
    const char* systems[48];
    int ns = 0;
    auto addSystem = [&](const char* sys) {
        if (!sys[0]) return;
        for (int i = 0; i < ns; ++i) if (_stricmp(systems[i], sys) == 0) return;
        if (ns < 48) systems[ns++] = sys;
    };
    if (here[0]) addSystem(here);
    for (int i = 0; i < np; ++i) addSystem(places[i].system);
    for (int i = 0; i < nm; ++i) addSystem(marks[i].system);
    std::sort(systems + (here[0] ? 1 : 0), systems + ns, [](const char* a, const char* b) { return _stricmp(a, b) < 0; });

    if (placesChanged) {                           // OOC_Stanton_1, 1a, 1b, 2 ... reads in orbit order
        for (int i = 0; i < np; ++i) order[i] = i;
        std::sort(order, order + np, [&](int a, int b) { return _stricmp(places[a].entity, places[b].entity) < 0; });
    }

    SearchBox("##travelFilter", "搜索地点和已保存的位置", filter, sizeof(filter));

    SectionHeading("地点");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##altitude", &altitude, 100.0f, 20000.0f, "到达时离地 %.0f 米", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("去知道半径的行星和卫星时：先在目标上空测到地面，再把你放到离地这个高度，之后自由下落，下落中可以随意转动视角。\n"
                          "其他地点不按离地算：空间站、城市等放在目标旁边（最多上方 50 米），不知道半径的星球放在轨道上。\n"
                          "这个值只在本次游戏里记住，重开游戏回到 2000 米。");
    static const char* const kLandingModes[] = { "降落在向阳面（白天）", "降落在北极", "降落在南极", "降落在指定经纬度" };
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##landing", &landing.mode, kLandingModes, static_cast<int>(sizeof(kLandingModes) / sizeof(kLandingModes[0])));
    ImGui::SetItemTooltip("去行星和卫星时落在哪里（只对知道半径的星球有效）。\n"
                          "向阳面：在恒星正下方那条经线上，到达时是白天。\n"
                          "北极：以前的落点；不少星球的极地是荒地，比如微科的北极没有雪。\n"
                          "到达后会等星球加载好、测到地面，再把你放到离地上面设的高度。");
    if (landing.mode == Landing_Sunlit || landing.mode == Landing_LatLon) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##latitude", &landing.latitude, -89.0f, 89.0f, "纬度 %.0f°（正数在北半球）");
    }
    if (landing.mode == Landing_LatLon) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##longitude", &landing.longitude, -180.0f, 180.0f, "经度 %.0f°");
        ImGui::SetItemTooltip("经纬度按星球自身的坐标计算，跟着星球自转，同一组数字总是同一块地面；"
                              "和游戏里其他地图上的坐标不一定一样。");
    }
    TravelPosition pos;
    if (Travel_CurrentPosition(pos)) {
        ImGui::TextDisabled("你在 %s：纬度 %.1f°，经度 %.1f°，离基准半径 %.0f 米", ZhPlace(pos.body, pos.body), pos.latitude,
                            pos.longitude, pos.altitude);
        ImGui::SameLine();
        if (ImGui::SmallButton("用这个经纬度")) {
            landing.mode = Landing_LatLon;
            landing.latitude = static_cast<float>(pos.latitude);
            landing.longitude = static_cast<float>(pos.longitude);
        }
        ImGui::SetItemTooltip("把降落位置设成你现在所在的经纬度，下次从别处去这颗星球时落在这里。");
    }
    ImGui::Checkbox("显示室内和小区域", &showMinor);
    ImGui::SetItemTooltip("电梯大厅、机库、小行星带分段等，默认隐藏。");
    const TravelPlace* pick = nullptr;
    bool go = false;
    for (int s = 0; s < ns; ++s) {
        int shown = 0;
        for (int k = 0; k < np; ++k) {
            const TravelPlace& p = places[order[k]];
            if (p.kind == Place_Minor && !showMinor) continue;
            if (_stricmp(p.system, systems[s]) == 0 && (!searching || MatchesFilter(p.name, filter) || MatchesFilter(p.entity, filter)
                                                        || MatchesFilter(ZhPlace(p.name, p.entity), filter))) ++shown;
        }
        if (!shown) continue;
        const bool isHere = here[0] && _stricmp(systems[s], here) == 0;
        char header[200];
        const bool unnamed = _strnicmp(systems[s], "SolarSystem", 11) == 0;
        snprintf(header, sizeof(header), "%s%s (%d)###sys_%s", unnamed ? "未命名星系" : ZhSystem(systems[s]),
                 isHere ? "，你在这里" : "", shown, systems[s]);
        if (searching) ImGui::SetNextItemOpen(true);
        if (!ImGui::CollapsingHeader(header, isHere ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
        char table[48];
        snprintf(table, sizeof(table), "##places_%s", systems[s]);
        const float rows = static_cast<float>(shown < 10 ? shown : 10);
        const float height = (rows + 1) * ImGui::GetFrameHeight() + 4;
        if (!ImGui::BeginTable(table, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY, ImVec2(0, height))) continue;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("地点", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableHeadersRow();
        for (int k = 0; k < np; ++k) {
            const TravelPlace& p = places[order[k]];
            if (_stricmp(p.system, systems[s]) != 0 || (p.kind == Place_Minor && !showMinor)) continue;
            if (searching && !MatchesFilter(p.name, filter) && !MatchesFilter(p.entity, filter)
                && !MatchesFilter(ZhPlace(p.name, p.entity), filter)) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char label[256];
            snprintf(label, sizeof(label), "%s##%s", ZhPlace(p.name, p.entity), p.entity);
            const bool isSel = _stricmp(selected, p.entity) == 0;
            if (ImGui::Selectable(label, isSel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                strcpy_s(selected, p.entity);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { pick = &p; go = true; }
            }
            ImGui::SetItemTooltip("%s", p.entity);
            ImGui::TableNextColumn();
            static const char* const kKind[] = { "行星", "卫星", "地点", "室内" };
            const int kindIdx = p.kind >= 0 && p.kind <= 3 ? p.kind : 2;
            ImGui::TextDisabled("%s%s", kKind[kindIdx], kindIdx <= Place_Moon && p.radius <= 0 ? "，轨道" : "");
        }
        ImGui::EndTable();
    }
    if (!pick)
        for (int i = 0; i < np; ++i)
            if (_stricmp(places[i].entity, selected) == 0) { pick = &places[i]; break; }
    char goLabel[240];
    snprintf(goLabel, sizeof(goLabel), pick ? "前往 %s" : "选择一个目的地", pick ? ZhPlace(pick->name, pick->entity) : "");
    ImGui::BeginDisabled(!pick);
    if (PrimaryButton(goLabel)) go = true;
    ImGui::EndDisabled();
    if (go && pick) {
        TravelPlace request = *pick;   // the name only shows in the status line
        CopyUtf8(request.name, sizeof(request.name), ZhPlace(pick->name, pick->entity));
        Travel_RequestPlace(request, altitude, landing);
    }

    float progress = 0;
    if (Travel_Scanning(progress)) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kLeafDeep);
        ImGui::ProgressBar(progress, ImVec2(-1, 0), "正在扫描...");
        ImGui::PopStyleColor();
    } else if (ImGui::Button("扫描游戏中的地点", ImVec2(-1, 0))) {
        Travel_RequestScan();
    }
    Hint("扫描会找出所有已加载星系中的行星、卫星、空间站、拉格朗日点、通讯阵列和跳跃点，"
         "并添加到这里。只读取不修改，需要几秒钟。");

    SectionHeading("已保存的位置");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 160 - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##markName", "给这个位置起个名字", markName, sizeof(markName));
    ImGui::SameLine();
    if (ImGui::Button("保存当前位置", ImVec2(160, 0))) { Travel_RequestSaveBookmark(markName); markName[0] = 0; }
    if (!nm) Hint("还没有保存任何位置。站到想去的地方，起个名字，再点“保存当前位置”。");
    for (int s = 0; s < ns; ++s) {
        int shown = 0;
        for (int i = 0; i < nm; ++i)
            if (_stricmp(marks[i].system, systems[s]) == 0 && (!searching || MatchesFilter(marks[i].name, filter))) ++shown;
        if (!shown) continue;
        char node[200];
        snprintf(node, sizeof(node), "%s (%d)###marks_%s", ZhSystem(systems[s]), shown, systems[s]);
        if (searching) ImGui::SetNextItemOpen(true);
        if (!ImGui::TreeNodeEx(node, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) continue;
        for (int i = 0; i < nm; ++i) {
            if (_stricmp(marks[i].system, systems[s]) != 0 || (searching && !MatchesFilter(marks[i].name, filter))) continue;
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(marks[i].name);
            const float buttons = 70 + 80 + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - buttons);
            if (ImGui::Button("前往", ImVec2(70, 0))) Travel_RequestBookmark(i);
            ImGui::SameLine();
            if (ImGui::Button("删除", ImVec2(80, 0))) Travel_RequestDeleteBookmark(i);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    Hint("F7/F8 仍可作为快速存档位使用。列表中的地点可以跨星系传送（实验功能，需用 PU_All 启动）；F8 和已保存的位置仍只能在当前星系内使用。");
}
