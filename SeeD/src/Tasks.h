#pragma once
#pragma warning( push )
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#include "../../Third/taskflow-master/taskflow/taskflow.hpp"
#include "../../Third/taskflow-master/taskflow/algorithm/for_each.hpp"
#pragma warning( pop )

static tf::Executor executor;
static tf::Taskflow taskflow;

// Two separate phases, easy to conflate:
//  1. BUILD (every line that calls TASK/MODULE/flow.emplace/.precede()/.succeed()): only registers
//     task nodes and graph edges -- none of the task bodies (the lambdas) execute yet.
//  2. RUN() (executor.run(taskflow).wait()): this is the ONLY point anything actually executes.
//     The executor runs whatever nodes currently have every predecessor satisfied, across worker
//     threads, in whatever order dependencies allow -- NOT the order tasks were registered in
//     during the build phase.
// So a task's C++ function body runs later than, and independently of, where its TASK(...) line
// appears in the code -- ordering between two tasks is decided ENTIRELY by .succeed()/.precede()
// edges, never by their registration order.
//
// The frame graph is built ONCE and re-run every frame (Engine::BuildSchedule): rebuilding it each
// frame (and the subflows it used to spawn, which Taskflow clears and re-emplaces on every run)
// was a node + std::function + name allocation per task per frame. It is rebuilt only when its
// shape changes (Engine::ScheduleKey, today the ECS system count). Consequences for anything that
// builds into it:
//  - no per-frame work in a Schedule function body: it runs once, at build. Per-frame setup
//    (queries, Reserve, Reset, ...) goes in its own task with edges to whatever consumes it.
//  - no per-frame values captured by value in a task lambda: capture `this` / references, and
//    keep frame values in members written by an upstream task. A for_each_index whose range
//    changes per frame takes its end through std::ref(member) (Taskflow reads it at run time).
//  - a group of tasks that must complete as a unit (what a subflow join used to give) is a
//    persistent tf::Taskflow added with MODULE()/composed_of: the module task completes only once
//    its whole graph has.
//
// Preferred style in this codebase: express real ordering requirements between operations as
// explicit tasks + .succeed()/.precede() edges (see Engine::ScheduleWorld for prefab/terrain
// streaming -> DeferredRelease -> ECS systems), not as sequential C++ calls inside one task's
// body that happen to work due to some other task's timing.

#define TASK_(namef, function) tf::Task namef = taskflow.emplace([this](){this->function();}).name(#namef)
#define TASK(function) TASK_(function, function)

// a persistent tf::Taskflow run as one task of the frame graph (not copied: it must outlive the graph)
#define MODULE(namef, flow) tf::Task namef = taskflow.composed_of(flow).name(#namef)

// NEED a tf::FlowBuilder& flow variable that exist via function parameter (likely)
#define SUBTASK_(namef, function) tf::Task namef = flow.emplace([this](){this->function();}).name(#namef)
#define SUBTASK(function) SUBTASK_(function, function)

#define SUBTASKWINDOW(window) tf::Task window##Task = flow.emplace([window](){window->Update();}).name(wstringToconstchar(window->name))

//#define LINK(S, T, P) T.succeed(S); P.succeed(T)
#define LINK(S, T) T.succeed(S)
#define RUN() {ZoneScoped;executor.run(taskflow).wait();}
#define CLEAR() {ZoneScoped;taskflow.clear();}
