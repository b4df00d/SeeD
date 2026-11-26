#pragma once

// Lap counter + timer: a start/finish line component, the ECS system that times every car across
// it, and the in-game HUD (a GameWindow, UI.h) that shows the player's laps.

#include <chrono>
#include <deque>
#include <unordered_map>

namespace Components
{
    // A start/finish gate. Centred on the entity's local X axis, from the entity's origin (local
    // y = 0) up to `height`; you drive through it along local +Z. Scale is ignored: width and
    // height are metres. Only the first StartLine in the world is used.
    struct StartLine : ComponentBase<StartLine>
    {
        float width = 4.0f;     // m, across the track (local X)
        float height = 2.0f;    // m, above the entity origin (local Y)
    };
    static void StartLinePropertyDraw(char* p)
    {
        StartLine* line = (StartLine*)p;
        ImGui::DragFloat("width", &line->width, 0.05f, 0.1f, 1000.0f, "%.2f m");
        ImGui::SetItemTooltip("Gate width across the track (local X), centred on the entity. Metres, scale is ignored.");
        ImGui::DragFloat("height", &line->height, 0.05f, 0.1f, 1000.0f, "%.2f m");
        ImGui::SetItemTooltip("Gate height above the entity origin (local Y). Metres, scale is ignored.\n"
                              "Drive through it along the entity's local +Z (the arrow) for a lap to count.");
    }
}

// The gate as world-space lines (rectangle + direction arrow), for the editor gizmo (Editor.h).
template<typename DrawLine>
static void StartLineGizmo(World::Entity ent, DrawLine drawLine)
{
    const Components::StartLine& line = ent.Get<Components::StartLine>();
    float4x4 world = ComputeWorldMatrix(ent);
    float3 origin = world[3].xyz;
    float3 x = normalize(world[0].xyz) * (line.width * 0.5f);
    float3 y = normalize(world[1].xyz) * line.height;
    float3 z = normalize(world[2].xyz);

    const ImU32 gateCol = IM_COL32(255, 200, 60, 255);
    const ImU32 arrowCol = IM_COL32(90, 255, 120, 255);
    float3 a = origin - x, b = origin + x, c = origin + x + y, d = origin - x + y;
    drawLine(a, b, gateCol);
    drawLine(b, c, gateCol);
    drawLine(c, d, gateCol);
    drawLine(d, a, gateCol);

    float arrowLength = std::max(0.5f, line.width * 0.25f);
    float3 tail = origin + y * 0.5f;
    float3 tip = tail + z * arrowLength;
    float3 side = normalize(x) * (arrowLength * 0.25f);
    drawLine(tail, tip, arrowCol);
    drawLine(tip, tip - z * (arrowLength * 0.3f) + side, arrowCol);
    drawLine(tip, tip - z * (arrowLength * 0.3f) - side, arrowCol);
}

namespace Systems
{
    // Times every car (Vehicle or RC buggy chassis) across the first StartLine, one record per car
    // so AI opponents / other players get their own. A lap counts when the car crosses the line
    // forward with nothing voiding it since the previous crossing:
    //  - a reset (Driver::ResetVehicle: R, the pad's reset button, falling off the world),
    //  - crossing the line backwards.
    // The voided lap is dropped and the next forward crossing starts a fresh one. Stopping play
    // also drops the running lap (the car may get moved in the editor meanwhile).
    //
    // runAfter Driver: Driver reads the reset input and teleports the car through its Transform this
    // frame, so reading the pose and Driver::resetsThisFrame in parallel with it would race.
    struct LapTimer : SystemBase
    {
        static LapTimer* instance;

        using Clock = std::chrono::steady_clock;
        static constexpr uint maxShownLaps = 10;

        struct CarLaps
        {
            World::Entity car;
            std::deque<double> laps;        // seconds, newest first, at most maxShownLaps
            double best = 0.0;              // over every valid lap since Reset, 0 = none yet
            double sum = 0.0;
            uint count = 0;                 // valid laps since Reset (average = sum / count)
            bool lapRunning = false;        // crossed the line at least once, a lap is being timed
            bool lapValid = false;          // false once the running lap was voided
            Clock::time_point lapStart;
            float3 prevPos = float3(0, 0, 0);
            bool prevValid = false;

            double CurrentLapSeconds() const
            {
                return lapRunning ? std::chrono::duration<double>(Clock::now() - lapStart).count() : 0.0;
            }
            double Average() const { return count > 0 ? sum / count : 0.0; }
        };

        std::unordered_map<uint, CarLaps> cars;     // by entity id; the stored entity's rev is checked
        bool hasStartLine = false;
        bool wasPlaying = false;

        // The HUD, owned and fed by the system.
        struct Window : GameWindow
        {
            void Update() override;
        };
        Window window;

        void On() override
        {
            instance = this;
            runAfter = { Driver::instance };
            World::instance->systems.push_back(this);
            GameWindow::Register(&window);
        }

        void Off() override
        {
            GameWindow::Unregister(&window);
            auto item = std::find(World::instance->systems.begin(), World::instance->systems.end(), this);
            if (item != World::instance->systems.end())
                World::instance->systems.erase(item);
            instance = nullptr;
        }

        // Clears every car's results and drops the laps being timed (they restart at the next crossing).
        void Reset()
        {
            for (auto& [id, car] : cars)
            {
                World::Entity keep = car.car;
                float3 pos = car.prevPos;
                bool posValid = car.prevValid;
                car = CarLaps();
                car.car = keep;
                car.prevPos = pos;
                car.prevValid = posValid;
            }
        }

        // The record of the car the player drives (as of this frame's Update), or null.
        World::Entity playerVehicle;
        const CarLaps* PlayerCar()
        {
            World::Entity vehicle = playerVehicle;
            if (vehicle.id == entityInvalid.id || !vehicle.IsValid())
                return nullptr;
            auto found = cars.find(vehicle.id);
            return found != cars.end() && found->second.car.rev == vehicle.rev ? &found->second : nullptr;
        }

        void Update(World* world) override
        {
            ZoneScoped;

            if (!engineState.playing)
            {
                wasPlaying = false;
                return;
            }
            bool playStarted = !wasPlaying;
            wasPlaying = true;
            playerVehicle = Driver::FindVehicle(world);

            // The first StartLine.
            uint lineQuery = world->Query(Components::StartLine::mask, 0, true);
            auto& lines = world->frameQueries[lineQuery];
            hasStartLine = !lines.empty();
            float3 lineOrigin, lineX, lineY, lineZ;
            float halfWidth = 0.0f, height = 0.0f;
            if (hasStartLine)
            {
                World::Entity line = lines[0];
                float4x4 lineWorld = ComputeWorldMatrix(line);
                lineOrigin = lineWorld[3].xyz;
                lineX = normalize(lineWorld[0].xyz);
                lineY = normalize(lineWorld[1].xyz);
                lineZ = normalize(lineWorld[2].xyz);
                halfWidth = line.Get<Components::StartLine>().width * 0.5f;
                height = line.Get<Components::StartLine>().height;
            }

            // Every car: road vehicles and RC buggy chassis.
            std::vector<World::Entity> vehicles;
            for (Components::Mask mask : { Components::Vehicle::mask, Components::RCBuggy::mask })
            {
                uint query = world->Query(mask, 0, true);
                for (World::Entity& ent : world->frameQueries[query])
                    vehicles.push_back(ent);
            }

            // Drop the records of cars that are gone (or whose slot was reused).
            for (auto it = cars.begin(); it != cars.end();)
            {
                bool alive = false;
                for (const World::Entity& v : vehicles)
                    if (v.id == it->first && v.rev == it->second.car.rev) { alive = true; break; }
                it = alive ? std::next(it) : cars.erase(it);
            }

            Clock::time_point now = Clock::now();
            for (World::Entity& vehicle : vehicles)
            {
                CarLaps& car = cars[vehicle.id];
                car.car = vehicle;

                if (playStarted)
                {
                    car.lapRunning = false;
                    car.lapValid = false;
                    car.prevValid = false;
                }

                // Reset: the lap is void, and no crossing test across the teleport -- a reset right
                // behind the line must neither finish nor start a lap.
                if (Driver::WasResetThisFrame(vehicle))
                {
                    car.lapValid = false;
                    car.prevValid = false;
                    continue;
                }

                float3 position;
                quaternion rotation;
                PhysicsGetWorldPose(vehicle, position, rotation);

                if (hasStartLine && car.prevValid)
                {
                    float3 p0 = car.prevPos - lineOrigin;
                    float3 p1 = position - lineOrigin;
                    float z0 = (float)dot(p0, lineZ);
                    float z1 = (float)dot(p1, lineZ);
                    bool forward = z0 < 0.0f && z1 >= 0.0f;
                    bool backward = z0 >= 0.0f && z1 < 0.0f;
                    if (forward || backward)
                    {
                        // Where the segment pierces the gate plane: inside the rectangle?
                        float t = z0 / (z0 - z1);
                        float3 hit = p0 + (p1 - p0) * t;
                        float hx = (float)dot(hit, lineX);
                        float hy = (float)dot(hit, lineY);
                        if (fabsf(hx) <= halfWidth && hy >= 0.0f && hy <= height)
                        {
                            if (forward)
                            {
                                if (car.lapRunning && car.lapValid)
                                {
                                    double lap = std::chrono::duration<double>(now - car.lapStart).count();
                                    car.laps.push_front(lap);
                                    if (car.laps.size() > maxShownLaps)
                                        car.laps.pop_back();
                                    car.sum += lap;
                                    car.count++;
                                    if (car.best == 0.0 || lap < car.best)
                                        car.best = lap;
                                }
                                car.lapStart = now;
                                car.lapRunning = true;
                                car.lapValid = true;
                            }
                            else
                            {
                                car.lapValid = false;
                            }
                        }
                    }
                }

                car.prevPos = position;
                car.prevValid = true;
            }
        }
    };
    LapTimer* LapTimer::instance = nullptr;
}

// "1:23.456" (or "23.456" under a minute)
static void FormatLapTime(double seconds, char* out, size_t size)
{
    if (seconds < 0.0) seconds = 0.0;
    uint totalMs = (uint)(seconds * 1000.0 + 0.5);
    uint minutes = totalMs / 60000;
    uint secs = (totalMs / 1000) % 60;
    uint ms = totalMs % 1000;
    if (minutes > 0)
        snprintf(out, size, "%u:%02u.%03u", minutes, secs, ms);
    else
        snprintf(out, size, "%u.%03u", secs, ms);
}

void Systems::LapTimer::Window::Update()
{
    LapTimer* timer = LapTimer::instance;
    if (timer == nullptr || World::instance == nullptr)
        return;
    const CarLaps* car = timer->PlayerCar();
    if (!timer->hasStartLine && (car == nullptr || car->count == 0))
        return;     // nothing to time on this track

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 24.0f, viewport->WorkPos.y + 24.0f),
        ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(260.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("Lap Timer##game", nullptr, GameWindow::baseFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    char buffer[32];
    char buffer2[32];

    // ---- current lap, big
    {
        bool pushed = GameStyle::PushFont(GameStyle::fontLarge, 2.2f);
        if (car == nullptr || !car->lapRunning)
        {
            ImGui::TextColored(GameStyle::dim, "--:--.---");
        }
        else
        {
            FormatLapTime(car->CurrentLapSeconds(), buffer, sizeof(buffer));
            ImGui::TextColored(car->lapValid ? GameStyle::text : GameStyle::dim, "%s", buffer);
        }
        GameStyle::PopFont(pushed);
    }

    bool smallFont = GameStyle::PushFont(GameStyle::fontSmall, 1.25f);

    if (!timer->hasStartLine)
        ImGui::TextColored(GameStyle::red, "No start line");
    else if (car == nullptr)
        ImGui::TextColored(GameStyle::dim, "No car");
    else if (!car->lapRunning)
        ImGui::TextColored(GameStyle::dim, "Cross the line to start");
    else if (!car->lapValid)
        ImGui::TextColored(GameStyle::red, "INVALID - car reset / wrong way");
    else
        ImGui::TextColored(GameStyle::dim, "LAP %u", car->count + 1);

    ImGui::Separator();

    // ---- best / average
    if (car != nullptr && car->count > 0)
    {
        FormatLapTime(car->best, buffer, sizeof(buffer));
        FormatLapTime(car->Average(), buffer2, sizeof(buffer2));
        ImGui::TextColored(GameStyle::dim, "BEST");
        ImGui::SameLine(90.0f);
        ImGui::TextColored(GameStyle::gold, "%s", buffer);
        ImGui::TextColored(GameStyle::dim, "AVG");
        ImGui::SameLine(90.0f);
        ImGui::Text("%s", buffer2);
        ImGui::SameLine();
        ImGui::TextColored(GameStyle::dim, "(%u laps)", car->count);
    }
    else
    {
        ImGui::TextColored(GameStyle::dim, "BEST");
        ImGui::SameLine(90.0f);
        ImGui::TextColored(GameStyle::dim, "-");
        ImGui::TextColored(GameStyle::dim, "AVG");
        ImGui::SameLine(90.0f);
        ImGui::TextColored(GameStyle::dim, "-");
    }

    // ---- last laps, newest first
    if (car != nullptr && !car->laps.empty())
    {
        ImGui::Separator();
        if (ImGui::BeginTable("laps", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            for (uint i = 0; i < (uint)car->laps.size(); i++)
            {
                double lap = car->laps[i];
                bool isBest = lap == car->best;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(GameStyle::dim, "%u", car->count - i);
                ImGui::TableSetColumnIndex(1);
                FormatLapTime(lap, buffer, sizeof(buffer));
                ImGui::TextColored(isBest ? GameStyle::gold : GameStyle::text, "%s", buffer);
                ImGui::TableSetColumnIndex(2);
                if (isBest)
                    ImGui::TextColored(GameStyle::gold, "best");
                else
                {
                    FormatLapTime(lap - car->best, buffer, sizeof(buffer));
                    ImGui::TextColored(GameStyle::red, "+%s", buffer);
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset"))
        timer->Reset();

    GameStyle::PopFont(smallFont);
    ImGui::End();
}
