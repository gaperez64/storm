#ifndef STORM_SOLVER_SYMBOLICGAMESOLVER_H_
#define STORM_SOLVER_SYMBOLICGAMESOLVER_H_

#include <set>
#include <vector>

#include "src/storm/solver/AbstractGameSolver.h"
#include "src/storm/solver/OptimizationDirection.h"

#include "src/storm/storage/expressions/Variable.h"
#include "src/storm/storage/dd/Bdd.h"
#include "src/storm/storage/dd/Add.h"

namespace storm {
    namespace solver {
        
        /*!
         * An interface that represents an abstract symbolic game solver.
         */
        template<storm::dd::DdType Type, typename ValueType = double>
        class SymbolicGameSolver : public AbstractGameSolver<ValueType> {
        public:
            /*!
             * Constructs a symbolic game solver with the given meta variable sets and pairs.
             *
             * @param gameMatrix The matrix defining the coefficients of the game.
             * @param allRows A BDD characterizing all rows of the equation system.
             * @param rowMetaVariables The meta variables used to encode the rows of the matrix.
             * @param columnMetaVariables The meta variables used to encode the columns of the matrix.
             * @param rowColumnMetaVariablePairs The pairs of row meta variables and the corresponding column meta
             * variables.
             * @param player1Variables The meta variables used to encode the player 1 choices.
             * @param player2Variables The meta variables used to encode the player 2 choices.
             */
            SymbolicGameSolver(storm::dd::Add<Type, ValueType> const& gameMatrix, storm::dd::Bdd<Type> const& allRows, std::set<storm::expressions::Variable> const& rowMetaVariables, std::set<storm::expressions::Variable> const& columnMetaVariables, std::vector<std::pair<storm::expressions::Variable, storm::expressions::Variable>> const& rowColumnMetaVariablePairs, std::set<storm::expressions::Variable> const& player1Variables, std::set<storm::expressions::Variable> const& player2Variables);
            
            /*!
             * Constructs a symbolic game solver with the given meta variable sets and pairs.
             *
             * @param gameMatrix The matrix defining the coefficients of the game.
             * @param allRows A BDD characterizing all rows of the equation system.
             * @param rowMetaVariables The meta variables used to encode the rows of the matrix.
             * @param columnMetaVariables The meta variables used to encode the columns of the matrix.
             * @param rowColumnMetaVariablePairs The pairs of row meta variables and the corresponding column meta
             * variables.
             * @param player1Variables The meta variables used to encode the player 1 choices.
             * @param player2Variables The meta variables used to encode the player 2 choices.
             * @param precision The precision to achieve.
             * @param maximalNumberOfIterations The maximal number of iterations to perform when solving a linear
             * equation system iteratively.
             * @param relative Sets whether or not to use a relativ stopping criterion rather than an absolute one.
             */
            SymbolicGameSolver(storm::dd::Add<Type, ValueType> const& gameMatrix, storm::dd::Bdd<Type> const& allRows, std::set<storm::expressions::Variable> const& rowMetaVariables, std::set<storm::expressions::Variable> const& columnMetaVariables, std::vector<std::pair<storm::expressions::Variable, storm::expressions::Variable>> const& rowColumnMetaVariablePairs, std::set<storm::expressions::Variable> const& player1Variables, std::set<storm::expressions::Variable> const& player2Variables, double precision, uint_fast64_t maximalNumberOfIterations, bool relative);
            
            /*!
             * Solves the equation system defined by the game matrix. Note that the game matrix has to be given upon
             * construction time of the solver object.
             *
             * @param player1Goal Sets whether player 1 wants to minimize or maximize.
             * @param player2Goal Sets whether player 2 wants to minimize or maximize.
             * @param x The initial guess of the solution.
             * @param b The vector to add after matrix-vector multiplication.
             * @return The solution vector.
             */
            virtual storm::dd::Add<Type, ValueType> solveGame(OptimizationDirection player1Goal, OptimizationDirection player2Goal, storm::dd::Add<Type, ValueType> const& x, storm::dd::Add<Type, ValueType> const& b) const;
        
        protected:
            // The matrix defining the coefficients of the linear equation system.
            storm::dd::Add<Type, ValueType> const& gameMatrix;
            
            // A BDD characterizing all rows of the equation system.
            storm::dd::Bdd<Type> const& allRows;
            
            // The row variables.
            std::set<storm::expressions::Variable> const& rowMetaVariables;
            
            // The column variables.
            std::set<storm::expressions::Variable> const& columnMetaVariables;
            
            // The pairs of meta variables used for renaming.
            std::vector<std::pair<storm::expressions::Variable, storm::expressions::Variable>> const& rowColumnMetaVariablePairs;
            
            // The player 1 variables.
            std::set<storm::expressions::Variable> const& player1Variables;
            
            // The player 2 variables.
            std::set<storm::expressions::Variable> const& player2Variables;
        };
        
    } // namespace solver
} // namespace storm

#endif /* STORM_SOLVER_SYMBOLICGAMESOLVER_H_ */