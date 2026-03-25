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
        float busyPercent;
        float waitingPercent;
        float idlePercent;
        uint64_t totalNs;
    };

    void drawProfilerUI() {
        ThreadProfiler& profiler = ThreadProfiler::instance();
        Vector<SDL_ThreadID> threadIds = profiler.getAllThreadIds();

        ImGui::Text("Frame: %llu", (unsigned long long)profiler.getFrameNumber());
        ImGui::Separator();

        if (ImGui::BeginTable("ThreadStats", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Thread");
            ImGui::TableSetupColumn("Busy %");
            ImGui::TableSetupColumn("Waiting %");
            ImGui::TableSetupColumn("Idle %");
            ImGui::TableSetupColumn("Busy Graph");
            ImGui::TableSetupColumn("Waiting Graph");
            ImGui::TableSetupColumn("Idle Graph");
            ImGui::TableHeadersRow();

            for (uint64_t i = 0; i < threadIds.size(); ++i) {
                SDL_ThreadID threadId = threadIds[i];
                ThreadStats stats;

                if (!profiler.getThreadStats(threadId, &stats)) {
                    continue;
                }

                ThreadUIData data;
                data.threadId = threadId;
                SDL_strlcpy(data.threadName, stats.threadName, sizeof(data.threadName));

                // Calculate total time and percentages from rolling average
                uint64_t avgBusyTime = 0;
                uint64_t avgWaitingTime = 0;
                uint64_t avgIdleTime = 0;

                for (int j = 0; j < ThreadStats::HISTORY_SIZE; ++j) {
                    avgBusyTime += stats.history[j].stateTime[THREAD_STATE_BUSY];
                    avgWaitingTime += stats.history[j].stateTime[THREAD_STATE_WAITING];
                    avgIdleTime += stats.history[j].stateTime[THREAD_STATE_IDLE];
                }

                data.totalNs = avgBusyTime + avgWaitingTime + avgIdleTime;

                if (data.totalNs > 0) {
                    data.busyPercent = (float)(avgBusyTime * 100.0 / data.totalNs);
                    data.waitingPercent = (float)(avgWaitingTime * 100.0 / data.totalNs);
                    data.idlePercent = (float)(avgIdleTime * 100.0 / data.totalNs);
                } else {
                    data.busyPercent = data.waitingPercent = data.idlePercent = 0.0f;
                }

                drawThreadRow(data);
            }

            ImGui::EndTable();
        }

        // Legend
        ImGui::Separator();
        ImGui::Text("Legend:");
        ImGui::SameLine();
        ImGui::ColorButton("##BusyColor", ImVec4(0.8f, 0.2f, 0.2f, 1.0f), ImGuiColorEditFlags_NoPicker);
        ImGui::SameLine();
        ImGui::Text("Busy");
        ImGui::SameLine(150);
        ImGui::ColorButton("##WaitingColor", ImVec4(0.8f, 0.8f, 0.2f, 1.0f), ImGuiColorEditFlags_NoPicker);
        ImGui::SameLine();
        ImGui::Text("Waiting");
        ImGui::SameLine(300);
        ImGui::ColorButton("##IdleColor", ImVec4(0.2f, 0.8f, 0.2f, 1.0f), ImGuiColorEditFlags_NoPicker);
        ImGui::SameLine();
        ImGui::Text("Idle");
    }

    void drawThreadRow(const ThreadUIData& data) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s (ID: %llu)", data.threadName, (unsigned long long)data.threadId);

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.1f%%", data.busyPercent);

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.1f%%", data.waitingPercent);

        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%.1f%%", data.idlePercent);

        // Draw horizontal bar graphs for visual representation
        ImGui::TableSetColumnIndex(4);
        drawProgressBar(data.busyPercent, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));

        ImGui::TableSetColumnIndex(5);
        drawProgressBar(data.waitingPercent, ImVec4(0.8f, 0.8f, 0.2f, 1.0f));

        ImGui::TableSetColumnIndex(6);
        drawProgressBar(data.idlePercent, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
    }

    void drawProgressBar(float percent, ImVec4 color) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        char buf[32];
        SDL_snprintf(buf, sizeof(buf), " %.0f%%##progbar_%p", percent, &percent);
        ImGui::ProgressBar(percent / 100.0f, ImVec2(60, 0), buf);
        ImGui::PopStyleColor();
    }
};
