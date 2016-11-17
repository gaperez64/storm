#ifndef STORM_STORAGE_EXPRESSIONS_EXPRESSION_H_
#define STORM_STORAGE_EXPRESSIONS_EXPRESSION_H_

#include <memory>
#include <map>
#include <unordered_map>
#include <vector>

#include "src/storm/storage/expressions/BaseExpression.h"
#include "src/storm/utility/OsDetection.h"

namespace storm {
    namespace expressions {
        // Foward-declare expression manager class.
        class ExpressionManager;
        class Variable;
        class ExpressionVisitor;
        
        class Expression {
        public:
            friend class ExpressionManager;
            friend class Variable;
            template<typename MapType> friend class SubstitutionVisitor;
            
            friend Expression operator+(Expression const& first, Expression const& second);
            friend Expression operator+(Expression const& first, int64_t second);
            friend Expression operator+(int64_t first, Expression const& second);
            friend Expression operator-(Expression const& first, Expression const& second);
            friend Expression operator-(Expression const& first, int64_t second);
            friend Expression operator-(int64_t first, Expression const& second);
            friend Expression operator-(Expression const& first);
            friend Expression operator*(Expression const& first, Expression const& second);
            friend Expression operator/(Expression const& first, Expression const& second);
            friend Expression operator^(Expression const& first, Expression const& second);
            friend Expression operator&&(Expression const& first, Expression const& second);
            friend Expression operator||(Expression const& first, Expression const& second);
            friend Expression operator!(Expression const& first);
            friend Expression operator==(Expression const& first, Expression const& second);
            friend Expression operator!=(Expression const& first, Expression const& second);
            friend Expression operator>(Expression const& first, Expression const& second);
            friend Expression operator>=(Expression const& first, Expression const& second);
            friend Expression operator<(Expression const& first, Expression const& second);
            friend Expression operator<=(Expression const& first, Expression const& second);
            friend Expression operator>(Expression const& first, int64_t second);
            friend Expression operator>=(Expression const& first, int64_t second);
            friend Expression operator<(Expression const& first, int64_t second);
            friend Expression operator<=(Expression const& first, int64_t second);
            friend Expression ite(Expression const& condition, Expression const& thenExpression, Expression const& elseExpression);
            friend Expression implies(Expression const& first, Expression const& second);
            friend Expression iff(Expression const& first, Expression const& second);
            friend Expression xclusiveor(Expression const& first, Expression const& second);
            friend Expression abs(Expression const& first);
            friend Expression truncate(Expression const& first);
            friend Expression sign(Expression const& first);
            friend Expression floor(Expression const& first);
            friend Expression ceil(Expression const& first);
            friend Expression minimum(Expression const& first, Expression const& second);
            friend Expression maximum(Expression const& first, Expression const& second);

            Expression() = default;
            
            /*!
             * Creates an expression representing the given variable.
             *
             * @param variable The variable to represent.
             */
            Expression(Variable const& variable);
            
            // Instantiate constructors and assignments with their default implementations.
            Expression(Expression const& other) = default;
            Expression& operator=(Expression const& other) = default;
#ifndef WINDOWS
            Expression(Expression&&) = default;
            Expression& operator=(Expression&&) = default;
#endif
            
            /*!
             * Substitutes all occurrences of the variables according to the given map. Note that this substitution is
             * done simultaneously, i.e., variables appearing in the expressions that were "plugged in" are not
             * substituted.
             *
             * @param variableToExpressionMap A mapping from variables to the expression they are substituted with.
             * @return An expression in which all identifiers in the key set of the mapping are replaced by the
             * expression they are mapped to.
             */
            Expression substitute(std::map<Variable, Expression> const& variableToExpressionMap) const;

            /*!
            * Substitutes all occurrences of the variables according to the given map. Note that this substitution is
            * done simultaneously, i.e., variables appearing in the expressions that were "plugged in" are not
            * substituted.
            *
            * @param variableToExpressionMap A mapping from variables to the expression they are substituted with.
            * @return An expression in which all identifiers in the key set of the mapping are replaced by the
            * expression they are mapped to.
            */
            Expression substitute(std::unordered_map<Variable, Expression> const& variableToExpressionMap) const;
            
            /*!
             * Evaluates the expression under the valuation of variables given by the valuation and returns the
             * resulting boolean value. If the return type of the expression is not a boolean an exception is thrown.
             *
             * @param valuation The valuation of unknowns under which to evaluate the expression.
             * @return The boolean value of the expression under the given valuation.
             */
            bool evaluateAsBool(Valuation const* valuation = nullptr) const;
            
            /*!
             * Evaluates the expression under the valuation of variables given by the valuation and returns the
             * resulting integer value. If the return type of the expression is not an integer an exception is thrown.
             *
             * @param valuation The valuation of unknowns under which to evaluate the expression.
             * @return The integer value of the expression under the given valuation.
             */
            int_fast64_t evaluateAsInt(Valuation const* valuation = nullptr) const;
            
            /*!
             * Evaluates the expression under the valuation of variables given by the valuation and returns the
             * resulting double value. If the return type of the expression is not a double an exception is thrown.
             *
             * @param valuation The valuation of unknowns under which to evaluate the expression.
             * @return The double value of the expression under the given valuation.
             */
            double evaluateAsDouble(Valuation const* valuation = nullptr) const;
            
            /*!
             * Simplifies the expression according to some basic rules.
             *
             * @return The simplified expression.
             */
            Expression simplify() const;
            
            /*!
             * Retrieves the operator of a function application. This is only legal to call if the expression is
             * function application.
             *
             * @return The operator associated with the function application.
             */
            OperatorType getOperator() const;
            
            /*!
             * Checks if the expression is a function application (of any sort).
             *
             * @return True iff the expression is a function application.
             */
            bool isFunctionApplication() const;
            
            /*!
             * Retrieves the arity of the expression.
             *
             * @return The arity of the expression.
             */
            uint_fast64_t getArity() const;
            
            /*!
             * Retrieves the given operand from the expression.
             *
             * @param operandIndex The index of the operand to retrieve. This must be lower than the arity of the expression.
             * @return The operand at the given index.
             */
            Expression getOperand(uint_fast64_t operandIndex) const;
            
            /*!
             * Retrieves the identifier associated with this expression. This is only legal to call if the expression
             * is a variable.
             *
             * @return The identifier associated with this expression.
             */
            std::string const& getIdentifier() const;
            
            /*!
             * Retrieves whether the expression contains a variable.
             *
             * @return True iff the expression contains a variable.
             */
            bool containsVariables() const;
            
            /*!
             * Retrieves whether the expression is a literal.
             *
             * @return True iff the expression is a literal.
             */
            bool isLiteral() const;
            
            /*!
             * Retrieves whether the expression is a variable.
             *
             * @return True iff the expression is a variable.
             */
            bool isVariable() const;
            
            /*!
             * Checks if the expression is equal to the boolean literal true.
             *
             * @return True iff the expression is equal to the boolean literal true.
             */
            bool isTrue() const;
            
            /*!
             * Checks if the expression is equal to the boolean literal false.
             *
             * @return True iff the expression is equal to the boolean literal false.
             */
            bool isFalse() const;
            
            /*!
             * Retrieves whether this expression is a relation expression, i.e., an expression that has a relation
             * (equal, not equal, less, less or equal, etc.) as its top-level operator.
             *
             * @return True iff the expression is a relation expression.
             */
            bool isRelationalExpression() const;
            
            /*!
             * Retrieves whether this expression is a linear expression.
             *
             * @return True iff the expression is linear.
             */
            bool isLinear() const;
            
            /*!
             * Retrieves the set of all variables that appear in the expression.
             *
             * @return The set of all variables that appear in the expression.
             */
            std::set<storm::expressions::Variable> getVariables() const;

            /*!
             * Retrieves whether the expression contains any of the given variables.
             *
             * @param variables The variables to search for.
             * @return True iff any of the variables appear in the expression.
             */
            bool containsVariable(std::set<storm::expressions::Variable> const& variables) const;
            
            /*!
             * Retrieves the base expression underlying this expression object. Note that prior to calling this, the
             * expression object must be properly initialized.
             *
             * @return A reference to the underlying base expression.
             */
            BaseExpression const& getBaseExpression() const;
            
            /*!
             * Retrieves a pointer to the base expression underlying this expression object.
             *
             * @return A pointer to the underlying base expression.
             */
            std::shared_ptr<BaseExpression const> const& getBaseExpressionPointer() const;

            /*!
             * Retrieves the manager responsible for this expression.
             *
             * @return The manager responsible for this expression.
             */
            ExpressionManager const& getManager() const;
            
            /*!
             * Retrieves the type of the expression.
             *
             * @return The type of the expression.
             */
            Type const& getType() const;
            
            /*!
             * Retrieves whether the expression has a numerical return type, i.e., integer or double.
             *
             * @return True iff the expression has a numerical return type.
             */
            bool hasNumericalType() const;

            /*!
             * Retrieves whether the expression has a rational return type.
             *
             * @return True iff the expression has a rational return type.
             */
            bool hasRationalType() const;
            
            /*!
             * Retrieves whether the expression has a boolean return type.
             *
             * @return True iff the expression has a boolean return type.
             */
            bool hasBooleanType() const;
            
            /*!
             * Retrieves whether the expression has an integral return type.
             *
             * @return True iff the expression has a integral return type.
             */
            bool hasIntegerType() const;

            /*!
             * Retrieves whether the expression has an integral return type.
             *
             * @return True iff the expression has a integral return type.
             */
            bool hasBitVectorType() const;
            
            /*!
             * Accepts the given visitor.
             *
             * @param visitor The visitor to accept.
             */
            boost::any accept(ExpressionVisitor& visitor, boost::any const& data) const;
            
            /*!
             * Converts the expression into a string.
             *
             * @return The string representation of the expression.
             */
            std::string toString() const;

            /*!
             * Checks whether the object encapsulates a base-expression.
             */
            bool isInitialized() const;

            /*!
             * Checks whether the two expressions are syntatically the same.
             */
            bool isSyntacticallyEqual(storm::expressions::Expression const& other) const;
            
            friend std::ostream& operator<<(std::ostream& stream, Expression const& expression);

        private:
            /*!
             * Creates an expression with the given underlying base expression.
             *
             * @param expressionPtr A pointer to the underlying base expression.
             */
            Expression(std::shared_ptr<BaseExpression const> const& expressionPtr);
                        
            // A pointer to the underlying base expression.
            std::shared_ptr<BaseExpression const> expressionPtr;
        };
        
        // Provide operator overloads to conveniently construct new expressions from other expressions.
        Expression operator+(Expression const& first, Expression const& second);
        Expression operator+(Expression const& first, int64_t second);
        Expression operator+(int64_t first, Expression const& second);
        Expression operator-(Expression const& first, Expression const& second);
        Expression operator-(Expression const& first, int64_t second);
        Expression operator-(int64_t first, Expression const& second);
        Expression operator-(Expression const& first);
        Expression operator*(Expression const& first, Expression const& second);
        Expression operator/(Expression const& first, Expression const& second);
        Expression operator^(Expression const& first, Expression const& second);
        Expression operator&&(Expression const& first, Expression const& second);
        Expression operator||(Expression const& first, Expression const& second);
        Expression operator!(Expression const& first);
        Expression operator==(Expression const& first, Expression const& second);
        Expression operator!=(Expression const& first, Expression const& second);
        Expression operator>(Expression const& first, Expression const& second);
        Expression operator>=(Expression const& first, Expression const& second);
        Expression operator<(Expression const& first, Expression const& second);
        Expression operator<=(Expression const& first, Expression const& second);
        Expression operator>(Expression const& first, int64_t second);
        Expression operator>=(Expression const& first, int64_t second);
        Expression operator<(Expression const& first, int64_t second);
        Expression operator<=(Expression const& first, int64_t second);
        Expression ite(Expression const& condition, Expression const& thenExpression, Expression const& elseExpression);
        Expression implies(Expression const& first, Expression const& second);
        Expression iff(Expression const& first, Expression const& second);
        Expression xclusiveor(Expression const& first, Expression const& second);
        Expression abs(Expression const& first);
        Expression truncate(Expression const& first);
        Expression sign(Expression const& first);
        Expression floor(Expression const& first);
        Expression ceil(Expression const& first);
        Expression minimum(Expression const& first, Expression const& second);
        Expression maximum(Expression const& first, Expression const& second);
        Expression disjunction(std::vector<storm::expressions::Expression> const& expressions);
        Expression conjunction(std::vector<storm::expressions::Expression> const& expressions);
        Expression sum(std::vector<storm::expressions::Expression> const& expressions);
        Expression apply(std::vector<storm::expressions::Expression> const& expressions, std::function<Expression (Expression const&, Expression const&)> const& function);

    }
}

//specialize 
namespace std {
	template<>
	struct less < storm::expressions::Expression > {
		bool operator()(const storm::expressions::Expression& lhs, const storm::expressions::Expression& rhs) const {
			return lhs.getBaseExpressionPointer() < rhs.getBaseExpressionPointer();
		}
	};
}

#endif /* STORM_STORAGE_EXPRESSIONS_EXPRESSION_H_ */