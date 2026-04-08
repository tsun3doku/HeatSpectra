#include "NodeGraphParamUtils.hpp"

#include <algorithm>
#include <memory>
#include <utility>

NodeGraphParamField makeParamField(const char* name, NodeGraphParamDefinition definition) {
    NodeGraphParamField field{};
    field.name = name;
    field.definition = std::make_shared<NodeGraphParamDefinition>(std::move(definition));
    return field;
}

NodeGraphParamDefinition makeStructParamDefinition(
    uint32_t id,
    const char* name,
    std::vector<NodeGraphParamField> fields) {
    NodeGraphParamDefinition definition{};
    definition.id = id;
    definition.name = name;
    definition.type = NodeGraphParamType::Struct;
    definition.fields = std::move(fields);
    return definition;
}

NodeGraphParamDefinition makeArrayParamDefinition(
    uint32_t id,
    const char* name,
    NodeGraphParamDefinition elementDefinition) {
    NodeGraphParamDefinition definition{};
    definition.id = id;
    definition.name = name;
    definition.type = NodeGraphParamType::Array;
    definition.elementDefinition = std::make_shared<NodeGraphParamDefinition>(std::move(elementDefinition));
    return definition;
}

NodeGraphParamDefinition makeEnumParamDefinition(
    uint32_t id,
    const char* name,
    const char* defaultValue,
    std::vector<std::string> enumOptions) {
    NodeGraphParamDefinition definition{};
    definition.id = id;
    definition.name = name;
    definition.type = NodeGraphParamType::Enum;
    definition.defaultStringValue = defaultValue;
    definition.enumOptions = std::move(enumOptions);
    return definition;
}

NodeGraphParamDefinition makeIntParamDefinition(uint32_t id, const char* name, int64_t defaultValue) {
    NodeGraphParamDefinition definition{};
    definition.id = id;
    definition.name = name;
    definition.type = NodeGraphParamType::Int;
    definition.defaultIntValue = defaultValue;
    return definition;
}

NodeGraphParamFieldValue makeParamFieldValue(const char* name, NodeGraphParamValue value) {
    NodeGraphParamFieldValue fieldValue{};
    fieldValue.name = name ? name : "";
    fieldValue.value = std::make_shared<NodeGraphParamValue>(std::move(value));
    return fieldValue;
}

NodeGraphParamValue makeStructParamValue(std::initializer_list<NodeGraphParamFieldValue> fields) {
    NodeGraphParamValue value{};
    value.type = NodeGraphParamType::Struct;
    value.fieldValues.assign(fields.begin(), fields.end());
    return value;
}

NodeGraphParamValue makeArrayParamValue(uint32_t id, std::vector<NodeGraphParamValue> elements) {
    NodeGraphParamValue value{};
    value.id = id;
    value.type = NodeGraphParamType::Array;
    value.arrayValues = std::move(elements);
    return value;
}

NodeGraphParamValue makeIntParamValue(int64_t value) {
    NodeGraphParamValue parameter{};
    parameter.type = NodeGraphParamType::Int;
    parameter.intValue = value;
    return parameter;
}

NodeGraphParamValue makeStringParamValue(std::string value) {
    NodeGraphParamValue parameter{};
    parameter.type = NodeGraphParamType::String;
    parameter.stringValue = std::move(value);
    return parameter;
}

NodeGraphParamValue makeEnumParamValue(std::string value) {
    NodeGraphParamValue parameter{};
    parameter.type = NodeGraphParamType::Enum;
    parameter.enumValue = std::move(value);
    return parameter;
}

const NodeGraphParamValue* findParamFieldValue(const NodeGraphParamValue& value, const char* fieldName) {
    if (!fieldName || value.type != NodeGraphParamType::Struct) {
        return nullptr;
    }

    const auto it = std::find_if(
        value.fieldValues.begin(),
        value.fieldValues.end(),
        [fieldName](const NodeGraphParamFieldValue& fieldValue) {
            return fieldValue.name == fieldName && fieldValue.value;
        });
    return it != value.fieldValues.end() ? it->value.get() : nullptr;
}

NodeGraphParamValue* findParamFieldValue(NodeGraphParamValue& value, const char* fieldName) {
    if (!fieldName || value.type != NodeGraphParamType::Struct) {
        return nullptr;
    }

    const auto it = std::find_if(
        value.fieldValues.begin(),
        value.fieldValues.end(),
        [fieldName](const NodeGraphParamFieldValue& fieldValue) {
            return fieldValue.name == fieldName && fieldValue.value;
        });
    return it != value.fieldValues.end() ? it->value.get() : nullptr;
}

bool tryGetParamInt(const NodeGraphParamValue& value, int64_t& outValue) {
    if (value.type != NodeGraphParamType::Int) {
        return false;
    }
    outValue = value.intValue;
    return true;
}

bool tryGetParamString(const NodeGraphParamValue& value, std::string& outValue) {
    if (value.type != NodeGraphParamType::String) {
        return false;
    }
    outValue = value.stringValue;
    return true;
}

bool tryGetParamEnum(const NodeGraphParamValue& value, std::string& outValue) {
    if (value.type != NodeGraphParamType::Enum) {
        return false;
    }
    outValue = value.enumValue;
    return true;
}

bool setParamField(NodeGraphParamValue& value, const char* fieldName, NodeGraphParamValue fieldValue) {
    if (!fieldName || value.type != NodeGraphParamType::Struct) {
        return false;
    }

    NodeGraphParamValue* existingField = findParamFieldValue(value, fieldName);
    if (existingField) {
        *existingField = std::move(fieldValue);
        return true;
    }

    value.fieldValues.push_back(makeParamFieldValue(fieldName, std::move(fieldValue)));
    return true;
}

bool addArrayElement(NodeGraphParamValue& value, NodeGraphParamValue elementValue) {
    if (value.type != NodeGraphParamType::Array) {
        return false;
    }

    value.arrayValues.push_back(std::move(elementValue));
    return true;
}

bool removeArrayElement(NodeGraphParamValue& value, std::size_t index) {
    if (value.type != NodeGraphParamType::Array || index >= value.arrayValues.size()) {
        return false;
    }

    value.arrayValues.erase(value.arrayValues.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool replaceArrayElement(NodeGraphParamValue& value, std::size_t index, NodeGraphParamValue elementValue) {
    if (value.type != NodeGraphParamType::Array || index >= value.arrayValues.size()) {
        return false;
    }

    value.arrayValues[index] = std::move(elementValue);
    return true;
}

const NodeGraphParamValue* getArrayElement(const NodeGraphParamValue& value, std::size_t index) {
    if (value.type != NodeGraphParamType::Array || index >= value.arrayValues.size()) {
        return nullptr;
    }

    return &value.arrayValues[index];
}

NodeGraphParamValue* getArrayElement(NodeGraphParamValue& value, std::size_t index) {
    if (value.type != NodeGraphParamType::Array || index >= value.arrayValues.size()) {
        return nullptr;
    }

    return &value.arrayValues[index];
}
