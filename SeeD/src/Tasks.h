#pragma once
#pragma warning( push )
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#include "../../Third/taskflow-master/taskflow/taskflow.hpp"
#include "../../Third/taskflow-master/taskflow/algorithm/for_each.hpp"
#pragma warning( pop )

static tf::Executor executor;
static tf::Taskflow taskflow;

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