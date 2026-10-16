#pragma once

#include <string>

namespace zl::script {

// 本构建是否编入了 Python 脚本后端（ZL_ENABLE_PYTHON，M8-T1）。
bool IsPythonBackendCompiled();

// 描述字符串："CPython x.y.z / pybind11 a.b.c"；未编译时返回中文说明。
std::string PythonBackendVersion();

}  // namespace zl::script
