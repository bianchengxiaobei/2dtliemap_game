#include <doctest/doctest.h>

#include <string>

#include "script/PythonSupport.h"

using namespace zl;

TEST_CASE("M8：Python 后端编译开关（默认 OFF 不引入 Python 依赖）") {
#ifdef ZL_ENABLE_PYTHON
    CHECK(script::IsPythonBackendCompiled());
    CHECK(script::PythonBackendVersion().find("CPython") != std::string::npos);
    CHECK(script::PythonBackendVersion().find("pybind11") != std::string::npos);
#else
    CHECK_FALSE(script::IsPythonBackendCompiled());
    CHECK(script::PythonBackendVersion() == std::string("未编译（ZL_ENABLE_PYTHON=OFF）"));
#endif
}
