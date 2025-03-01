#include <Analyzer/Passes/IfTransformStringsToEnumPass.h>

#include <Analyzer/ConstantNode.h>
#include <Analyzer/FunctionNode.h>
#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/InDepthQueryTreeVisitor.h>

#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeEnum.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/IDataType.h>

#include <Functions/FunctionFactory.h>

namespace DB
{

namespace
{

/// We place strings in ascending order here under the assumption it could speed up String to Enum conversion.
template <typename EnumType>
auto getDataEnumType(const std::set<std::string> & string_values)
{
    using EnumValues = typename EnumType::Values;
    EnumValues enum_values;
    enum_values.reserve(string_values.size());

    size_t number = 1;
    for (const auto & value : string_values)
        enum_values.emplace_back(value, number++);

    return std::make_shared<EnumType>(std::move(enum_values));
}

DataTypePtr getEnumType(const std::set<std::string> & string_values)
{
    if (string_values.size() >= 255)
        return getDataEnumType<DataTypeEnum16>(string_values);
    else
        return getDataEnumType<DataTypeEnum8>(string_values);
}

QueryTreeNodePtr createCastFunction(QueryTreeNodePtr from, DataTypePtr result_type, ContextPtr context)
{
    auto enum_literal = std::make_shared<ConstantValue>(result_type->getName(), std::make_shared<DataTypeString>());
    auto enum_literal_node = std::make_shared<ConstantNode>(std::move(enum_literal));

    auto cast_function = FunctionFactory::instance().get("_CAST", std::move(context));
    QueryTreeNodes arguments{std::move(from), std::move(enum_literal_node)};

    auto function_node = std::make_shared<FunctionNode>("_CAST");
    function_node->resolveAsFunction(std::move(cast_function), std::move(result_type));
    function_node->getArguments().getNodes() = std::move(arguments);

    return function_node;
}

/// if(arg1, arg2, arg3) will be transformed to if(arg1, _CAST(arg2, Enum...), _CAST(arg3, Enum...))
/// where Enum is generated based on the possible values stored in string_values
void changeIfArguments(
    QueryTreeNodePtr & first, QueryTreeNodePtr & second, const std::set<std::string> & string_values, const ContextPtr & context)
{
    auto result_type = getEnumType(string_values);

    first = createCastFunction(first, result_type, context);
    second = createCastFunction(second, result_type, context);
}

/// transform(value, array_from, array_to, default_value) will be transformed to transform(value, array_from, _CAST(array_to, Array(Enum...)), _CAST(default_value, Enum...))
/// where Enum is generated based on the possible values stored in string_values
void changeTransformArguments(
    QueryTreeNodePtr & array_to,
    QueryTreeNodePtr & default_value,
    const std::set<std::string> & string_values,
    const ContextPtr & context)
{
    auto result_type = getEnumType(string_values);

    array_to = createCastFunction(array_to, std::make_shared<DataTypeArray>(result_type), context);
    default_value = createCastFunction(default_value, std::move(result_type), context);
}

void wrapIntoToString(FunctionNode & function_node, QueryTreeNodePtr arg, ContextPtr context)
{
    assert(isString(function_node.getResultType()));

    auto to_string_function = FunctionFactory::instance().get("toString", std::move(context));
    QueryTreeNodes arguments{std::move(arg)};

    function_node.resolveAsFunction(std::move(to_string_function), std::make_shared<DataTypeString>());
    function_node.getArguments().getNodes() = std::move(arguments);
}

class ConvertStringsToEnumVisitor : public InDepthQueryTreeVisitor<ConvertStringsToEnumVisitor>
{
public:
    explicit ConvertStringsToEnumVisitor(ContextPtr context_)
        : context(std::move(context_))
    {
    }

    void visitImpl(QueryTreeNodePtr & node)
    {
        auto * function_node = node->as<FunctionNode>();

        if (!function_node)
            return;

        /// to preserve return type (String) of the current function_node, we wrap the newly
        /// generated function nodes into toString

        std::string_view function_name = function_node->getFunctionName();
        if (function_name == "if")
        {
            if (function_node->getArguments().getNodes().size() != 3)
                return;

            auto modified_if_node = function_node->clone();
            auto & argument_nodes = modified_if_node->as<FunctionNode>()->getArguments().getNodes();

            const auto * first_literal = argument_nodes[1]->as<ConstantNode>();
            const auto * second_literal = argument_nodes[2]->as<ConstantNode>();

            if (!first_literal || !second_literal)
                return;

            if (!isString(first_literal->getResultType()) || !isString(second_literal->getResultType()))
                return;

            std::set<std::string> string_values;
            string_values.insert(first_literal->getValue().get<std::string>());
            string_values.insert(second_literal->getValue().get<std::string>());

            changeIfArguments(argument_nodes[1], argument_nodes[2], string_values, context);
            wrapIntoToString(*function_node, std::move(modified_if_node), context);
            return;
        }

        if (function_name == "transform")
        {
            if (function_node->getArguments().getNodes().size() != 4)
                return;

            auto modified_transform_node = function_node->clone();
            auto & argument_nodes = modified_transform_node->as<FunctionNode>()->getArguments().getNodes();

            if (!isString(function_node->getResultType()))
                return;

            const auto * literal_to = argument_nodes[2]->as<ConstantNode>();
            const auto * literal_default = argument_nodes[3]->as<ConstantNode>();

            if (!literal_to || !literal_default)
                return;

            if (!isArray(literal_to->getResultType()) || !isString(literal_default->getResultType()))
                return;

            auto array_to = literal_to->getValue().get<Array>();

            if (array_to.empty())
                return;

            if (!std::all_of(
                    array_to.begin(),
                    array_to.end(),
                    [](const auto & field) { return field.getType() == Field::Types::Which::String; }))
                return;

            /// collect possible string values
            std::set<std::string> string_values;

            for (const auto & value : array_to)
                string_values.insert(value.get<std::string>());

            string_values.insert(literal_default->getValue().get<std::string>());

            changeTransformArguments(argument_nodes[2], argument_nodes[3], string_values, context);
            wrapIntoToString(*function_node, std::move(modified_transform_node), context);
            return;
        }
    }

private:
    ContextPtr context;
};

}

void IfTransformStringsToEnumPass::run(QueryTreeNodePtr query, ContextPtr context)
{
    ConvertStringsToEnumVisitor visitor(context);
    visitor.visit(query);
}

}
