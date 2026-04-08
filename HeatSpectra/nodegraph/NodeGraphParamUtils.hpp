#pragma once

#include "NodeGraphTypes.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

NodeGraphParamField makeParamField(const char* name, NodeGraphParamDefinition definition);
NodeGraphParamDefinition makeStructParamDefinition(
    uint32_t id,
    const char* name,
    std::vector<NodeGraphParamField> fields);
NodeGraphParamDefinition makeArrayParamDefinition(
    uint32_t id,
    const char* name,
    NodeGraphParamDefinition elementDefinition);
NodeGraphParamDefinition makeEnumParamDefinition(
    uint32_t id,
    const char* name,
    const char* defaultValue,
    std::vector<std::string> enumOptions);
NodeGraphParamDefinition makeIntParamDefinition(uint32_t id, const char* name, int64_t defaultValue = 0);

NodeGraphParamFieldValue makeParamFieldValue(const char* name, NodeGraphParamValue value);
NodeGraphParamValue makeStructParamValue(std::initializer_list<NodeGraphParamFieldValue> fields);
NodeGraphParamValue makeArrayParamValue(uint32_t id, std::vector<NodeGraphParamValue> elements = {});
NodeGraphParamValue makeIntParamValue(int64_t value);
NodeGraphParamValue makeStringParamValue(std::string value);
NodeGraphParamValue makeEnumParamValue(std::string value);

const NodeGraphParamValue* findParamFieldValue(const NodeGraphParamValue& value, const char* fieldName);
NodeGraphParamValue* findParamFieldValue(NodeGraphParamValue& value, const char* fieldName);

bool tryGetParamInt(const NodeGraphParamValue& value, int64_t& outValue);
bool tryGetParamString(const NodeGraphParamValue& value, std::string& outValue);
bool tryGetParamEnum(const NodeGraphParamValue& value, std::string& outValue);

bool setParamField(NodeGraphParamValue& value, const char* fieldName, NodeGraphParamValue fieldValue);
bool addArrayElement(NodeGraphParamValue& value, NodeGraphParamValue elementValue);
bool removeArrayElement(NodeGraphParamValue& value, std::size_t index);
bool replaceArrayElement(NodeGraphParamValue& value, std::size_t index, NodeGraphParamValue elementValue);
const NodeGraphParamValue* getArrayElement(const NodeGraphParamValue& value, std::size_t index);
NodeGraphParamValue* getArrayElement(NodeGraphParamValue& value, std::size_t index);
