#pragma once

class UIHelpers
{
public:
    static void DrawHandle(EntityBase& handleTarget, Components::Mask dataTemplateType);
};

void AlignForWidth(float width, float alignment = 0.5f)
{
    ImGuiStyle& style = ImGui::GetStyle();
    float avail = ImGui::GetContentRegionAvail().x;
    float off = (avail - width) * alignment;
    if (off > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
}

// Split button: a main part showing buttonLabels[selected], and a chevron part that opens the list of
// `items` (the long descriptions). Returns `selected` when the main part is clicked, -1 otherwise.
// Picking an item in the list only changes `selected` (the caller keeps it, typically a static int) --
// the main part runs it. The main part is as wide as the widest button label, so the widget does not
// resize when the selection changes. `tooltips` (optional, one per item) go on the main part, for the
// selected item (an ImGui::SetItemTooltip after the call would land on the chevron).
int DropDownButton(const char* id, int& selected, const char* const* items, const char* const* buttonLabels, int itemCount, const char* const* tooltips = nullptr)
{
    if (itemCount <= 0)
        return -1;
    if (selected < 0 || selected >= itemCount)
        selected = 0;

    ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGui::PushID(id);

    const char* label = buttonLabels[selected];
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float widestLabel = 0.0f;
    for (int i = 0; i < itemCount; i++)
    {
        float w = ImGui::CalcTextSize(buttonLabels[i]).x;
        widestLabel = w > widestLabel ? w : widestLabel;
    }
    float height = ImGui::GetFrameHeight();
    float rounding = style.FrameRounding > height * 0.3f ? style.FrameRounding : height * 0.3f;
    float mainWidth = widestLabel + style.FramePadding.x;
    float arrowWidth = height * 0.85f;
    float separator = 1.0f;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 mainMin = pos;
    ImVec2 mainMax = ImVec2(pos.x + mainWidth, pos.y + height);
    ImVec2 arrowMin = ImVec2(mainMax.x + separator, pos.y);
    ImVec2 arrowMax = ImVec2(arrowMin.x + arrowWidth, pos.y + height);

    int result = -1;
    if (ImGui::InvisibleButton("##main", ImVec2(mainWidth, height)))
        result = selected;
    bool mainHovered = ImGui::IsItemHovered();
    bool mainHeld = ImGui::IsItemActive();
    if (tooltips != nullptr && tooltips[selected] != nullptr)
        ImGui::SetItemTooltip("%s", tooltips[selected]);
    ImGui::SameLine(0.0f, separator);
    if (ImGui::InvisibleButton("##arrow", ImVec2(arrowWidth, height)))
        ImGui::OpenPopup("##dropdown");
    bool arrowHovered = ImGui::IsItemHovered();
    bool arrowHeld = ImGui::IsItemActive();
    bool open = ImGui::IsPopupOpen("##dropdown");

    // The chevron half is a shade darker than the main half, like a pressed-in segment.
    auto shade = [](ImU32 color, float factor)
    {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
        return ImGui::GetColorU32(ImVec4(c.x * factor, c.y * factor, c.z * factor, c.w));
    };
    auto buttonColor = [](bool hovered, bool held)
    {
        return ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    };
    drawList->AddRectFilled(mainMin, mainMax, buttonColor(mainHovered, mainHeld), rounding, ImDrawFlags_RoundCornersLeft);
    drawList->AddRectFilled(arrowMin, arrowMax, shade(buttonColor(arrowHovered || open, arrowHeld), 0.82f), rounding, ImDrawFlags_RoundCornersRight);

    drawList->AddText(ImVec2(mainMin.x + (mainWidth - textSize.x) * 0.5f, mainMin.y + (height - textSize.y) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_Text), label);

    // Chevron: up while the list is open, down otherwise.
    ImVec2 center = ImVec2((arrowMin.x + arrowMax.x) * 0.5f, (arrowMin.y + arrowMax.y) * 0.5f);
    float halfW = height * 0.16f;
    float halfH = halfW * 0.55f * (open ? -1.0f : 1.0f);
    ImVec2 chevron[3] = {
        ImVec2(center.x - halfW, center.y - halfH),
        ImVec2(center.x, center.y + halfH),
        ImVec2(center.x + halfW, center.y - halfH) };
    drawList->AddPolyline(chevron, 3, ImGui::GetColorU32(ImGuiCol_Text), ImDrawFlags_None, height * 0.08f > 1.5f ? height * 0.08f : 1.5f);

    // The list: right-aligned under the button, at least as wide as it.
    float totalWidth = arrowMax.x - mainMin.x;
    ImGui::SetNextWindowPos(ImVec2(arrowMax.x, arrowMax.y + style.ItemSpacing.y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(totalWidth, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.FramePadding.x, style.FramePadding.x));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    if (ImGui::BeginPopup("##dropdown"))
    {
        float itemHeight = height * 1.2f;
        float dotSpace = height * 0.8f;
        // The labels are drawn by hand, so the popup cannot size itself from them: give every row
        // the widest label's width (or the button's, whichever is larger).
        float itemWidth = totalWidth - style.FramePadding.x * 2.0f;
        for (int i = 0; i < itemCount; i++)
        {
            float w = dotSpace + ImGui::CalcTextSize(items[i]).x + style.FramePadding.x * 2.0f;
            itemWidth = w > itemWidth ? w : itemWidth;
        }
        for (int i = 0; i < itemCount; i++)
        {
            ImGui::PushID(i);
            ImVec2 itemPos = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##item", false, ImGuiSelectableFlags_None, ImVec2(itemWidth, itemHeight)))
                selected = i;
            ImDrawList* popupDrawList = ImGui::GetWindowDrawList();
            float midY = itemPos.y + itemHeight * 0.5f;
            if (i == selected)
                popupDrawList->AddCircleFilled(ImVec2(itemPos.x + dotSpace * 0.5f, midY), height * 0.14f, ImGui::GetColorU32(ImGuiCol_ButtonActive));
            ImVec2 itemText = ImGui::CalcTextSize(items[i]);
            popupDrawList->AddText(ImVec2(itemPos.x + dotSpace, midY - itemText.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), items[i]);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);

    ImGui::PopID();
    return result;
}