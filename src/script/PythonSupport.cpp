#include "script/PythonSupport.h"

#ifdef ZL_ENABLE_PYTHON
#include <pybind11/pybind11.h>
#endif

namespace zl::script {

bool IsPythonBackendCompiled() {
#ifdef ZL_ENABLE_PYTHON
    return true;
#else
    return false;
#endif
}

std::string PythonBackendVersion() {
#ifdef ZL_ENABLE_PYTHON
    // Py_GetVersion 不需要初始化解释器即可调用，用它验证与 CPython 的真实链接。
    std::string full = Py_GetVersion();
    const auto space = full.find(' ');
    if (space != std::string::npos) {
        full.resize(space);
    }
    return "CPython " + full + " / pybind11 " + std::to_string(PYBIND11_VERSION_MAJOR) + "." +
           std::to_string(PYBIND11_VERSION_MINOR) + "." + std::to_string(PYBIND11_VERSION_PATCH);
#else
    return "未编译（ZL_ENABLE_PYTHON=OFF）";
#endif
}

}  // namespace zl::script
