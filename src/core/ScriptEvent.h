#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace zl::core {

// 脚本事件载荷值（M7）：EventBus 事件 →「事件名 + 命名字段」的脚本层表示。
// 事件表 v1 见 docs/03 §5；字段中的 xxxId 一律是稳定 ID（StableId）。
using ScriptValue = std::variant<std::monostate, bool, int64_t, double, std::string>;

struct ScriptField {
    std::string name;
    ScriptValue value;
};

struct ScriptEvent {
    std::string name;                 // 事件名（OnXxx）
    std::vector<ScriptField> fields;  // 命名字段（顺序即声明顺序）

    const ScriptValue* Find(std::string_view fieldName) const {
        for (const ScriptField& field : fields) {
            if (field.name == fieldName) {
                return &field.value;
            }
        }
        return nullptr;
    }
};

}  // namespace zl::core
