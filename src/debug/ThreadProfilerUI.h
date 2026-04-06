#pragma once

#include <imgui.h>
#include "ThreadProfiler.h"

class ThreadProfilerUI {
public:
    ThreadProfilerUI() : isVisible_(true) {}

    void toggleVisibility() {
        isVisible_ = !isVisible_;
    }

    void setVisible(bool visible) {
        isVisible_ = visible;
    }

    bool isVisible() const {
        return isVisible_;
    }

    void draw() {
        if (!isVisible_) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Thread Performance Monitor", &isVisible_)) {
            drawProfilerUI();
        }
        ImGui::End();
    }

private:
    bool isVisible_;

    struct ThreadUIData {
        SDL_ThreadID threadId;
        char threadName[64];
        ThreadState currentState;
        float busyPercent;
        float waitingPercent;
        float idlePercent;
        float avgBusyMs;
        float avgWaitingMs;
        float avgIdleMs;
        int sampleCount;
    };

    static ImVec4 getStateColor(ThreadState state) {
        switch (state) {
            case THREAD_STATE_BUSY:
                return ImVec4(0.80f, 0.24f, 0.24f, 1.0f);
            case THREAD_STATE_WAITING:
                return ImVec4(0.82f, 0.72f, 0.22f, 1.0f);
            case THREAD_STATE_IDLE:
                return ImVec4(0.24f, 0.72f, 0.34f, 1.0f);
            default:
                return ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
        }
    }

    void drawProfilerUI() {
        ThreadProfiler& profiler = ThreadProfiler::instance();
        Vector<SDL_ThreadID> threadIds = profiler.getAllThreadIds();

        ImGui::Text("Frame: %llu", (unsigned long long)profiler.getFrameNumber());
        ImGui::TextWrapped("Percentages are sampled over the last %d completed frames. Busy means CPU work, Waiting means the thread is blocked on a fence, condition, or resource, and Idle means the thread intentionally slept or yielded.", ThreadStats::HISTORY_SIZE);
        ImGui::Separator();

        if (ImGui::BeginTable("ThreadStats", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthStretch, 2.3f);
            ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn("Busy", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("Waiting", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("Idle", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("Breakdown", ImGuiTableColumnFlags_WidthStretch, 2.2f);
            ImGui::TableHeadersRow();

            for (Uint64 i = 0; i < threadIds.size(); ++i) {
                SDL_ThreadID threadId = threadIds[i];
                ThreadStats stats;

                if (!profiler.getThreadStats(threadId, &stats)) {
                    continue;
                }

                ThreadUIData data;
                data.threadId = threadId;
                SDL_strlcpy(data.threadName, stats.threadName, sizeof(data.threadName));
                data.currentState = stats.currentState;

                Uint64 totalBusyTime = 0;
                Uint64 totalWaitingTime = 0;
                Uint64 totalIdleTime = 0;
                data.sampleCount = 0;

                for (int j = 0; j < ThreadStats::HISTORY_SIZE; ++j) {
                    if (stats.history[j].frameNumber == 0) {
                        continue;
                    }
                    totalBusyTime += stats.history[j].stateTime[THREAD_STATE_BUSY];
                    totalWaitingTime += stats.history[j].stateTime[THREAD_STATE_WAITING];
                    totalIdleTime += stats.history[j].stateTime[THREAD_STATE_IDLE];
                    ++data.sampleCount;
                }

                Uint64 totalNs = totalBusyTime + totalWaitingTime + totalIdleTime;

                if (totalNs > 0) {
                    data.busyPercent = (float)(totalBusyTime * 100.0 / totalNs);
                    data.waitingPercent = (float)(totalWaitingTime * 100.0 / totalNs);
                    data.idlePercent = (float)(totalIdleTime * 100.0 / totalNs);
                } else {
                    data.busyPercent = data.waitingPercent = data.idlePercent = 0.0f;
                }

                if (data.sampleCount > 0) {
                    data.avgBusyMs = (float)((double)totalBusyTime / (double)data.sampleCount / 1000000.0);
                    data.avgWaitingMs = (float)((double)totalWaitingTime / (double)data.sampleCount / 1000000.0);
                    data.avgIdleMs = (float)((double)totalIdleTime / (double)data.sampleCount / 1000000.0);
                } else {
                    data.avgBusyMs = 0.0f;
                    data.avgWaitingMs = 0.0f;
                    data.avgIdleMs = 0.0f;
                }

                drawThreadRow(data);
            }

            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("Legend:");
        ImGui::SameLine();
        ImGui::ColorButton("##BusyColor", getStateColor(THREAD_STATE_BUSY), ImGuiColorEditFlags_NoPicker);
        ImGui::SameLine();
        ImGui::Text("Busy / CPU work");
        ImGui::SameLine();
        ImGui::ColorButton("##WaitingColor", getStateColor(THREAD_STATE_WAITING), ImGuiColorEditFlags_NoPicker);
        ImGui::SameLine();
        ImGui::Text("Waiting / blocked");
        ImGui::SameLine();
        ImGui::ColorButton("##IdleColor", getStateColor(THREAD_STATE_IDLE), ImGuiColorEditFlags_NoPicker);
        ImGui::SameLine();
        ImGui::Text("Idle / sleep");
    }

    void drawThreadRow(const ThreadUIData& data) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s (ID: %llu)", data.threadName, (unsigned long long)data.threadId);

        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(getStateColor(data.currentState), "%s", getThreadStateName(data.currentState));

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.1f%%  %.2f ms", data.busyPercent, data.avgBusyMs);

        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%.1f%%  %.2f ms", data.waitingPercent, data.avgWaitingMs);

        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.1f%%  %.2f ms", data.idlePercent, data.avgIdleMs);

        ImGui::TableSetColumnIndex(5);
        drawBreakdownBar(data);
    }

    void drawBreakdownBar(const ThreadUIData& data) {
        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 18.0f);
        if (size.x < 150.0f) {
            size.x = 150.0f;
        }

        ImVec2 min = ImGui::GetCursorScreenPos();
        ImVec2 max = ImVec2(min.x + size.x, min.y + size.y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddRectFilled(min, max, IM_COL32(40, 40, 40, 255), 3.0f);

        float startX = min.x;
        const float segmentPercents[THREAD_STATE_COUNT] = {
            data.busyPercent,
            data.waitingPercent,
            data.idlePercent
        };

        for (int state = 0; state < THREAD_STATE_COUNT; ++state) {
            float width = size.x * (segmentPercents[state] / 100.0f);
            if (width <= 0.0f) {
                continue;
            }

            ImVec2 segMin(startX, min.y);
            ImVec2 segMax(startX + width, max.y);
            ImVec4 color = getStateColor((ThreadState)state);
            drawList->AddRectFilled(segMin, segMax, ImGui::ColorConvertFloat4ToU32(color), 3.0f);
            startX += width;
        }

        drawList->AddRect(min, max, IM_COL32(90, 90, 90, 255), 3.0f);

        char overlay[32];
        SDL_snprintf(overlay, sizeof(overlay), "%d frames", data.sampleCount);
        ImVec2 textSize = ImGui::CalcTextSize(overlay);
        drawList->AddText(ImVec2(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f), IM_COL32(255, 255, 255, 255), overlay);

        ImGui::Dummy(size);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", data.threadName);
            ImGui::Separator();
            ImGui::Text("Busy: %.1f%% (%.2f ms/frame)", data.busyPercent, data.avgBusyMs);
            ImGui::Text("Waiting: %.1f%% (%.2f ms/frame)", data.waitingPercent, data.avgWaitingMs);
            ImGui::Text("Idle: %.1f%% (%.2f ms/frame)", data.idlePercent, data.avgIdleMs);
            ImGui::EndTooltip();
        }
    }
};
