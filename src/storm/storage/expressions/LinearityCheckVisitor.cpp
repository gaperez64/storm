#include "storm/storage/expressions/LinearityCheckVisitor.h"

#include "storm/storage/expressions/Expressions.h"

#include "storm/utility/macros.h"
#include "storm/exceptions/InvalidTypeException.h"
#include "storm/exceptions/InvalidOperationException.h"

namespace storm {
    namespace expressions {
        LinearityCheckVisitor::LinearityCheckVisitor() {
            // Intentionally left empty.
        }
        
        bool LinearityCheckVisitor::check(Expression const& expression) {
            LinearityStatus result = boost::any_cast<LinearityStatus>(expression.getBaseExpression().accept(*this, boost::none));
            return result == LinearityStatus::LinearWithoutVariables || result == LinearityStatus::LinearContainsVariables;
        }
        
        boost::any LinearityCheckVisitor::visit(IfThenElseExpression const&, boost::any const&) {
            // An if-then-else expression is never linear.
            return LinearityStatus::NonLinear;
        }
        
        boost::any LinearityCheckVisitor::visit(BinaryBooleanFunctionExpression const&, boost::any const&) {
            // Boolean function applications are not allowed in linear expressions.
            return LinearityStatus::NonLinear;
        }
        
        boost::any LinearityCheckVisitor::visit(BinaryNumericalFunctionExpression const& expression, boost::any const& data) {
            LinearityStatus leftResult = boost::any_cast<LinearityStatus>(expression.getFirstOperand()->accept(*this, data));
            if (leftResult == LinearityStatus::NonLinear) {
                return LinearityStatus::NonLinear;
            }

            LinearityStatus rightResult = boost::any_cast<LinearityStatus>(expression.getSecondOperand()->accept(*this, data));
            if (rightResult == LinearityStatus::NonLinear) {
                return LinearityStatus::NonLinear;
            }
            
            switch (expression.getOperatorType()) {
                case BinaryNumericalFunctionExpression::OperatorType::Plus:
                case BinaryNumericalFunctionExpression::OperatorType::Minus:
                    return (leftResult == LinearityStatus::LinearContainsVariables || rightResult == LinearityStatus::LinearContainsVariables ? LinearityStatus::LinearContainsVariables : LinearityStatus::LinearWithoutVariables);
                case BinaryNumericalFunctionExpression::OperatorType::Times:
                case BinaryNumericalFunctionExpression::OperatorType::Divide:
                    if (leftResult == LinearityStatus::LinearContainsVariables && rightResult == LinearityStatus::LinearContainsVariables) {
                        return LinearityStatus::NonLinear;
                    }
                    
                    return (leftResult == LinearityStatus::LinearContainsVariables || rightResult == LinearityStatus::LinearContainsVariables ? LinearityStatus::LinearContainsVariables : LinearityStatus::LinearWithoutVariables);
                case BinaryNumericalFunctionExpression::OperatorType::Min: return LinearityStatus::NonLinear; break;
                case BinaryNumericalFunctionExpression::OperatorType::Max: return LinearityStatus::NonLinear; break;
                case BinaryNumericalFunctionExpression::OperatorType::Power: return LinearityStatus::NonLinear; break;
            }
            STORM_LOG_THROW(false, storm::exceptions::InvalidOperationException, "Illegal binary numerical expression operator.");
        }
        
        boost::any LinearityCheckVisitor::visit(BinaryRelationExpression const&, boost::any const&) {
            return LinearityStatus::NonLinear;
        }
        
        boost::any LinearityCheckVisitor::visit(VariableExpression const&, boost::any const&) {
            return LinearityStatus::LinearContainsVariables;
        }
        
        boost::any LinearityCheckVisitor::visit(UnaryBooleanFunctionExpression const&, boost::any const&) {
            // Boolean function applications are not allowed in linear expressions.
            return LinearityStatus::NonLinear;
        }
        
        boost::any LinearityCheckVisitor::visit(UnaryNumericalFunctionExpression const& expression, boost::any const& data) {
            switch (expression.getOperatorType()) {
                case UnaryNumericalFunctionExpression::OperatorType::Minus: return expression.getOperand()->accept(*this, data);
                case UnaryNumericalFunctionExpression::OperatorType::Floor:
                case UnaryNumericalFunctionExpression::OperatorType::Ceil: return LinearityStatus::NonLinear;
            }
            STORM_LOG_THROW(false, storm::exceptions::InvalidOperationException, "Illegal unary numerical expression operator.");
        }
        
        boost::any LinearityCheckVisitor::visit(BooleanLiteralExpression const&, boost::any const&) {
            return LinearityStatus::NonLinear;
        }
        
        boost::any LinearityCheckVisitor::visit(IntegerLiteralExpression const&, boost::any const&) {
            return LinearityStatus::LinearWithoutVariables;
        }
        
        boost::any LinearityCheckVisitor::visit(RationalLiteralExpression const&, boost::any const&) {
            return LinearityStatus::LinearWithoutVariables;
        }
    }
}