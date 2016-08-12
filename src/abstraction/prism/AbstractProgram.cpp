#include "src/abstraction/prism/AbstractProgram.h"

#include "src/abstraction/BottomStateResult.h"

#include "src/storage/prism/Program.h"

#include "src/storage/dd/DdManager.h"
#include "src/storage/dd/Add.h"

#include "src/models/symbolic/StandardRewardModel.h"

#include "src/utility/macros.h"
#include "src/utility/solver.h"
#include "src/exceptions/WrongFormatException.h"
#include "src/exceptions/InvalidArgumentException.h"

namespace storm {
    namespace abstraction {
        namespace prism {
            
            template <storm::dd::DdType DdType, typename ValueType>
            AbstractProgram<DdType, ValueType>::AbstractProgram(storm::prism::Program const& program,
                                                                std::vector<storm::expressions::Expression> const& initialPredicates,
                                                                std::shared_ptr<storm::utility::solver::SmtSolverFactory> const& smtSolverFactory,
                                                                bool addAllGuards)
            : program(program), smtSolverFactory(smtSolverFactory), abstractionInformation(program.getManager()), modules(), initialStateAbstractor(abstractionInformation, program.getAllExpressionVariables(), {program.getInitialConstruct().getInitialStatesExpression()}, this->smtSolverFactory), addedAllGuards(addAllGuards), currentGame(nullptr) {
                
                // For now, we assume that there is a single module. If the program has more than one module, it needs
                // to be flattened before the procedure.
                STORM_LOG_THROW(program.getNumberOfModules() == 1, storm::exceptions::WrongFormatException, "Cannot create abstract program from program containing too many modules.");
                
                // Add all variables and range expressions to the information object.
                for (auto const& variable : this->program.get().getAllExpressionVariables()) {
                    abstractionInformation.addExpressionVariable(variable);
                }
                for (auto const& range : this->program.get().getAllRangeExpressions()) {
                    abstractionInformation.addConstraint(range);
                    initialStateAbstractor.constrain(range);
                }
                
                uint_fast64_t totalNumberOfCommands = 0;
                uint_fast64_t maximalUpdateCount = 0;
                std::vector<storm::expressions::Expression> allGuards;
                for (auto const& module : program.getModules()) {
                    // If we were requested to add all guards to the set of predicates, we do so now.
                    for (auto const& command : module.getCommands()) {
                        if (addAllGuards) {
                            allGuards.push_back(command.getGuardExpression());
                        }
                        maximalUpdateCount = std::max(maximalUpdateCount, static_cast<uint_fast64_t>(command.getNumberOfUpdates()));
                    }
                
                    totalNumberOfCommands += module.getNumberOfCommands();
                }
                
                // NOTE: currently we assume that 100 player 2 variables suffice, which corresponds to 2^100 possible
                // choices. If for some reason this should not be enough, we could grow this vector dynamically, but
                // odds are that it's impossible to treat such models in any event.
                abstractionInformation.createEncodingVariables(static_cast<uint_fast64_t>(std::ceil(std::log2(totalNumberOfCommands))), 100, static_cast<uint_fast64_t>(std::ceil(std::log2(maximalUpdateCount))) + 1);
                
                // Now that we have created all other DD variables, we create the DD variables for the predicates.
                std::vector<uint_fast64_t> allPredicateIndices;
                if (addAllGuards) {
                    for (auto const& guard : allGuards) {
                        allPredicateIndices.push_back(abstractionInformation.addPredicate(guard));
                    }
                }
                for (auto const& predicate : initialPredicates) {
                    allPredicateIndices.push_back(abstractionInformation.addPredicate(predicate));
                }
                
                // For each module of the concrete program, we create an abstract counterpart.
                for (auto const& module : program.getModules()) {
                    this->modules.emplace_back(module, abstractionInformation, this->smtSolverFactory, addAllGuards);
                }
                
                // Refine the initial state abstractors using the initial predicates.
                initialStateAbstractor.refine(allPredicateIndices);
                
                // Retrieve the command-update probability ADD, so we can multiply it with the abstraction BDD later.
                commandUpdateProbabilitiesAdd = modules.front().getCommandUpdateProbabilitiesAdd();
                
                // Finally, we build the game the first time.
                currentGame = buildGame();
            }
            
            template <storm::dd::DdType DdType, typename ValueType>
            void AbstractProgram<DdType, ValueType>::refine(std::vector<storm::expressions::Expression> const& predicates) {
                STORM_LOG_THROW(!predicates.empty(), storm::exceptions::InvalidArgumentException, "Cannot refine without predicates.");
                
                // Add the predicates to the global list of predicates.
                std::vector<uint_fast64_t> newPredicateIndices;
                for (auto const& predicate : predicates) {
                    STORM_LOG_THROW(predicate.hasBooleanType(), storm::exceptions::InvalidArgumentException, "Expecting a predicate of type bool.");
                    uint_fast64_t newPredicateIndex = abstractionInformation.addPredicate(predicate);
                    newPredicateIndices.push_back(newPredicateIndex);
                }
                
                // Refine all abstract modules.
                for (auto& module : modules) {
                    module.refine(newPredicateIndices);
                }
                
                // Refine initial state abstractor.
                initialStateAbstractor.refine(newPredicateIndices);
                                
                // Finally, we rebuild the game.
                currentGame = buildGame();
            }
            
            template <storm::dd::DdType DdType, typename ValueType>
            MenuGame<DdType, ValueType> AbstractProgram<DdType, ValueType>::getAbstractGame() {
                STORM_LOG_ASSERT(currentGame != nullptr, "Game was not properly created.");
                return *currentGame;
            }
            
            template <storm::dd::DdType DdType, typename ValueType>
            storm::dd::Bdd<DdType> AbstractProgram<DdType, ValueType>::getStates(storm::expressions::Expression const& predicate) {
                STORM_LOG_ASSERT(currentGame != nullptr, "Game was not properly created.");
                return abstractionInformation.getPredicateSourceVariable(predicate);
            }
                        
            template <storm::dd::DdType DdType, typename ValueType>
            std::unique_ptr<MenuGame<DdType, ValueType>> AbstractProgram<DdType, ValueType>::buildGame() {
                // As long as there is only one module, we only build its game representation.
                GameBddResult<DdType> game = modules.front().getAbstractBdd();

                // Construct a set of all unnecessary variables, so we can abstract from it.
                std::set<storm::expressions::Variable> variablesToAbstract(abstractionInformation.getPlayer1VariableSet(abstractionInformation.getPlayer1VariableCount()));
                auto player2Variables = abstractionInformation.getPlayer2VariableSet(game.numberOfPlayer2Variables);
                variablesToAbstract.insert(player2Variables.begin(), player2Variables.end());
                auto auxVariables = abstractionInformation.getAuxVariableSet(1, abstractionInformation.getAuxVariableCount());
                variablesToAbstract.insert(auxVariables.begin(), auxVariables.end());
                std::cout << "vars" << std::endl;
                for (auto const& var : auxVariables) {
                    std::cout << var.getName() << std::endl;
                }
                
                // Do a reachability analysis on the raw transition relation.
                storm::dd::Bdd<DdType> transitionRelation = game.bdd.existsAbstract(variablesToAbstract);
                storm::dd::Bdd<DdType> initialStates = initialStateAbstractor.getAbstractStates();
                storm::dd::Bdd<DdType> reachableStates = this->getReachableStates(initialStates, transitionRelation);
                
                // Find the deadlock states in the model. Note that this does not find the 'deadlocks' in bottom states,
                // as the bottom states are not contained in the reachable states.
                storm::dd::Bdd<DdType> deadlockStates = transitionRelation.existsAbstract(abstractionInformation.getSuccessorVariables());
                deadlockStates = reachableStates && !deadlockStates;
                
                // If there are deadlock states, we fix them now.
                storm::dd::Add<DdType, ValueType> deadlockTransitions = abstractionInformation.getDdManager().template getAddZero<ValueType>();
                if (!deadlockStates.isZero()) {
                    deadlockTransitions = (deadlockStates && abstractionInformation.getAllPredicateIdentities() && abstractionInformation.encodePlayer1Choice(0, abstractionInformation.getPlayer1VariableCount()) && abstractionInformation.encodePlayer2Choice(0, game.numberOfPlayer2Variables) && abstractionInformation.encodeAux(0, 0, abstractionInformation.getAuxVariableCount())).template toAdd<ValueType>();
                }

                // Compute bottom states and the appropriate transitions if necessary.
                BottomStateResult<DdType> bottomStateResult(abstractionInformation.getDdManager().getBddZero(), abstractionInformation.getDdManager().getBddZero());
                if (!addedAllGuards) {
                    bottomStateResult = modules.front().getBottomStateTransitions(reachableStates, game.numberOfPlayer2Variables);
                }
                
                // Construct the transition matrix by cutting away the transitions of unreachable states.
                storm::dd::Add<DdType> transitionMatrix = (game.bdd && reachableStates).template toAdd<ValueType>() * commandUpdateProbabilitiesAdd + deadlockTransitions;// + bottomStateResult.transitions.template toAdd<ValueType>();
                transitionMatrix.exportToDot("trans_upd.dot");
            
                std::set<storm::expressions::Variable> usedPlayer2Variables(abstractionInformation.getPlayer2Variables().begin(), abstractionInformation.getPlayer2Variables().begin() + game.numberOfPlayer2Variables);
                
                std::set<storm::expressions::Variable> allNondeterminismVariables = usedPlayer2Variables;
                allNondeterminismVariables.insert(abstractionInformation.getPlayer1Variables().begin(), abstractionInformation.getPlayer1Variables().end());

                return std::make_unique<MenuGame<DdType, ValueType>>(abstractionInformation.getDdManagerAsSharedPointer(), reachableStates, initialStates, abstractionInformation.getDdManager().getBddZero(), transitionMatrix, bottomStateResult.states, abstractionInformation.getSourceVariables(), abstractionInformation.getSuccessorVariables(), abstractionInformation.getSourceSuccessorVariablePairs(), std::set<storm::expressions::Variable>(abstractionInformation.getPlayer1Variables().begin(), abstractionInformation.getPlayer1Variables().end()), usedPlayer2Variables, allNondeterminismVariables, auxVariables, abstractionInformation.getPredicateToBddMap());
            }
            
            template <storm::dd::DdType DdType, typename ValueType>
            storm::dd::Bdd<DdType> AbstractProgram<DdType, ValueType>::getReachableStates(storm::dd::Bdd<DdType> const& initialStates, storm::dd::Bdd<DdType> const& transitionRelation) {
                storm::dd::Bdd<DdType> frontier = initialStates;

                storm::dd::Bdd<DdType> reachableStates = initialStates;
                uint_fast64_t reachabilityIteration = 0;
                while (!frontier.isZero()) {
                    ++reachabilityIteration;
                    frontier = frontier.andExists(transitionRelation, abstractionInformation.getSourceVariables());
                    frontier = frontier.swapVariables(abstractionInformation.getSourceSuccessorVariablePairs());
                    frontier &= !reachableStates;
                    reachableStates |= frontier;
                    STORM_LOG_TRACE("Iteration " << reachabilityIteration << " of reachability analysis.");
                }
                
                return reachableStates;
            }
            
            // Explicitly instantiate the class.
            template class AbstractProgram<storm::dd::DdType::CUDD, double>;
            template class AbstractProgram<storm::dd::DdType::Sylvan, double>;
            
        }
    }
}