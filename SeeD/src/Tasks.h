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
//  1. BUILD (every line below that calls TASK/TASKWITHSUBFLOW/.precede()/.succeed()): runs
//     synchronously, once per frame, in normal C++ call order. This only registers task nodes and
//     graph edges -- none of the task bodies (the lambdas the macros wrap) execute yet.
//  2. RUN() (executor.run(taskflow).wait()): this is the ONLY point anything actually executes.
//     The executor runs whatever nodes currently have every predecessor satisfied, across worker
//     threads, in whatever order dependencies allow -- NOT the order tasks were registered in
//     during the build phase.
// So a task's C++ function body runs later than, and independently of, where its TASK(...) line
// appears in the code -- ordering between two tasks is decided ENTIRELY by .succeed()/.precede()
// edges, never by their registration order. Relying on "task A's setup code runs before task B's"
// just because A's TASK(...) line is textually earlier is not something Taskflow guarantees.
//
// TASKWITHSUBFLOW's lambda takes a tf::Subflow&: any further subflow.emplace(...) calls inside it
// create a NESTED graph. By default a subflow auto-joins its parent -- the outer task isn't
// considered complete until every task spawned in its subflow is (call subflow.detach() to opt
// out, not used anywhere in this codebase). So `B.succeed(A)` where A is a TASKWITHSUBFLOW task
// correctly waits for everything A's subflow spawned too, not just A's own immediate body.
//
// Preferred style in this codebase: express real ordering requirements between operations as
// explicit tasks + .succeed()/.precede() edges (see Main.cpp's ScheduleWorld for prefab/terrain
// streaming -> DeferredRelease -> ECS systems), not as plain sequential C++ calls inside one
// task's body that happen to work due to another task's subflow-join timing -- correct today, but
// implicit and easy to break silently in a later refactor.

#define TASK_(namef, function) tf::Task namef = taskflow.emplace([this](){this->function();}).name(#namef)
#define TASK(function) TASK_(function, function)

/*
#define TASKFLOW_(namef, function) tf::Task namef = taskflow.emplace([this](){this->function();}).name(#namef)
#define TASKFLOW(function) TASK_(function, function)
*/

#define TASKWITHSUBFLOW_(namef, function) tf::Task namef = taskflow.emplace([this](tf::Subflow& subflow){this->function(subflow);}).name(#namef)
#define TASKWITHSUBFLOW(function) TASKWITHSUBFLOW_(function, function)

// NEED a subflow variable that exist via function parameter (likely)
#define SUBTASK_(namef, function) tf::Task namef = subflow.emplace([this](){this->function();}).name(#namef)
#define SUBTASK(function) SUBTASK_(subflow, function, function)

#define SUBTASKWINDOW(window) tf::Task window##Task = subflow.emplace([window](){window->Update();}).name(wstringToconstchar(window->name))

//#define LINK(S, T, P) T.succeed(S); P.succeed(T)
#define LINK(S, T) T.succeed(S)
#define RUN() {ZoneScoped;executor.run(taskflow).wait();}
#define CLEAR() {ZoneScoped;taskflow.clear();}