#include "storm/modelchecker/csl/helper/SparseCtmcCslHelper.h"

#include "storm/modelchecker/prctl/helper/SparseDtmcPrctlHelper.h"
#include "storm/modelchecker/reachability/SparseDtmcEliminationModelChecker.h"

#include "storm/models/sparse/StandardRewardModel.h"

#include "storm/settings/SettingsManager.h"
#include "storm/settings/modules/GeneralSettings.h"

#include "storm/solver/LinearEquationSolver.h"
#include "storm/solver/Multiplier.h"

#include "storm/storage/StronglyConnectedComponentDecomposition.h"

#include "storm/adapters/RationalFunctionAdapter.h"

#include "storm/utility/macros.h"
#include "storm/utility/vector.h"
#include "storm/utility/graph.h"
#include "storm/utility/numerical.h"

#include "storm/exceptions/InvalidOperationException.h"
#include "storm/exceptions/InvalidStateException.h"
#include "storm/exceptions/InvalidPropertyException.h"
#include "storm/exceptions/FormatUnsupportedBySolverException.h"
#include "storm/exceptions/UncheckedRequirementException.h"
#include "storm/exceptions/NotSupportedException.h"

namespace storm {
    namespace modelchecker {
        namespace helper {
            template <typename ValueType, typename std::enable_if<storm::NumberTraits<ValueType>::SupportsExponential, int>::type>
            std::vector<ValueType> SparseCtmcCslHelper::computeBoundedUntilProbabilities(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::SparseMatrix<ValueType> const& backwardTransitions, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, std::vector<ValueType> const& exitRates, bool qualitative, double lowerBound, double upperBound) {
                
                uint_fast64_t numberOfStates = rateMatrix.getRowCount();
                
                // If the time bounds are [0, inf], we rather call untimed reachability.
                if (storm::utility::isZero(lowerBound) && upperBound == storm::utility::infinity<ValueType>()) {
                    return computeUntilProbabilities(env, std::move(goal), rateMatrix, backwardTransitions, exitRates, phiStates, psiStates, qualitative);
                }
                
                // From this point on, we know that we have to solve a more complicated problem [t, t'] with either t != 0
                // or t' != inf.
                
                // Create the result vector.
                std::vector<ValueType> result;
                
                // If we identify the states that have probability 0 of reaching the target states, we can exclude them from the
                // further computations.
                storm::storage::BitVector statesWithProbabilityGreater0 = storm::utility::graph::performProbGreater0(backwardTransitions, phiStates, psiStates);
                STORM_LOG_INFO("Found " << statesWithProbabilityGreater0.getNumberOfSetBits() << " states with probability greater 0.");
                storm::storage::BitVector statesWithProbabilityGreater0NonPsi = statesWithProbabilityGreater0 & ~psiStates;
                STORM_LOG_INFO("Found " << statesWithProbabilityGreater0NonPsi.getNumberOfSetBits() << " 'maybe' states.");
                
                if (!statesWithProbabilityGreater0.empty()) {
                    if (storm::utility::isZero(upperBound)) {
                        // In this case, the interval is of the form [0, 0].
                        result = std::vector<ValueType>(numberOfStates, storm::utility::zero<ValueType>());
                        storm::utility::vector::setVectorValues<ValueType>(result, psiStates, storm::utility::one<ValueType>());
                    } else {
                        if (storm::utility::isZero(lowerBound)) {
                            // In this case, the interval is of the form [0, t].
                            // Note that this excludes [0, inf] since this is untimed reachability and we considered this case earlier.
                            
                            result = std::vector<ValueType>(numberOfStates, storm::utility::zero<ValueType>());
                            storm::utility::vector::setVectorValues<ValueType>(result, psiStates, storm::utility::one<ValueType>());
                            if (!statesWithProbabilityGreater0NonPsi.empty()) {
                                // Find the maximal rate of all 'maybe' states to take it as the uniformization rate.
                                ValueType uniformizationRate = 0;
                                for (auto const& state : statesWithProbabilityGreater0NonPsi) {
                                    uniformizationRate = std::max(uniformizationRate, exitRates[state]);
                                }
                                uniformizationRate *= 1.02;
                                STORM_LOG_THROW(uniformizationRate > 0, storm::exceptions::InvalidStateException, "The uniformization rate must be positive.");
                                
                                // Compute the uniformized matrix.
                                storm::storage::SparseMatrix<ValueType> uniformizedMatrix = computeUniformizedMatrix(rateMatrix, statesWithProbabilityGreater0NonPsi, uniformizationRate, exitRates);
                                
                                // Compute the vector that is to be added as a compensation for removing the absorbing states.
                                std::vector<ValueType> b = rateMatrix.getConstrainedRowSumVector(statesWithProbabilityGreater0NonPsi, psiStates);
                                for (auto& element : b) {
                                    element /= uniformizationRate;
                                }
                                
                                // Finally compute the transient probabilities.
                                std::vector<ValueType> values(statesWithProbabilityGreater0NonPsi.getNumberOfSetBits(), storm::utility::zero<ValueType>());
                                std::vector<ValueType> subresult = computeTransientProbabilities(env, uniformizedMatrix, &b, upperBound, uniformizationRate, values);
                                storm::utility::vector::setVectorValues(result, statesWithProbabilityGreater0NonPsi, subresult);
                            }
                        } else if (upperBound == storm::utility::infinity<ValueType>()) {
                            // In this case, the interval is of the form [t, inf] with t != 0.
                            
                            // Start by computing the (unbounded) reachability probabilities of reaching psi states while
                            // staying in phi states.
                            result = computeUntilProbabilities(env, storm::solver::SolveGoal<ValueType>(), rateMatrix, backwardTransitions, exitRates, phiStates, psiStates, qualitative);
                            
                            // Determine the set of states that must be considered further.
                            storm::storage::BitVector relevantStates = statesWithProbabilityGreater0 & phiStates;
                            std::vector<ValueType> subResult(relevantStates.getNumberOfSetBits());
                            storm::utility::vector::selectVectorValues(subResult, relevantStates, result);
                            
                            ValueType uniformizationRate = 0;
                            for (auto const& state : relevantStates) {
                                uniformizationRate = std::max(uniformizationRate, exitRates[state]);
                            }
                            uniformizationRate *= 1.02;
                            STORM_LOG_THROW(uniformizationRate > 0, storm::exceptions::InvalidStateException, "The uniformization rate must be positive.");
                            
                            // Compute the uniformized matrix.
                            storm::storage::SparseMatrix<ValueType> uniformizedMatrix = computeUniformizedMatrix(rateMatrix, relevantStates, uniformizationRate, exitRates);
                            
                            // Compute the transient probabilities.
                            subResult = computeTransientProbabilities<ValueType>(env, uniformizedMatrix, nullptr, lowerBound, uniformizationRate, subResult);
                            
                            // Fill in the correct values.
                            storm::utility::vector::setVectorValues(result, ~relevantStates, storm::utility::zero<ValueType>());
                            storm::utility::vector::setVectorValues(result, relevantStates, subResult);
                        } else {
                            // In this case, the interval is of the form [t, t'] with t != 0 and t' != inf.
                            
                            if (lowerBound != upperBound) {
                                // In this case, the interval is of the form [t, t'] with t != 0, t' != inf and t != t'.
                                
                                
                                storm::storage::BitVector relevantStates = statesWithProbabilityGreater0 & phiStates;
                                std::vector<ValueType> newSubresult(relevantStates.getNumberOfSetBits(), storm::utility::zero<ValueType>());
                                storm::utility::vector::setVectorValues(newSubresult, psiStates % relevantStates, storm::utility::one<ValueType>());
                                if (!statesWithProbabilityGreater0NonPsi.empty()) {
                                    // Find the maximal rate of all 'maybe' states to take it as the uniformization rate.
                                    ValueType uniformizationRate = storm::utility::zero<ValueType>();
                                    for (auto const& state : statesWithProbabilityGreater0NonPsi) {
                                        uniformizationRate = std::max(uniformizationRate, exitRates[state]);
                                    }
                                    uniformizationRate *= 1.02;
                                    STORM_LOG_THROW(uniformizationRate > 0, storm::exceptions::InvalidStateException, "The uniformization rate must be positive.");
                                    
                                    // Compute the (first) uniformized matrix.
                                    storm::storage::SparseMatrix<ValueType> uniformizedMatrix = computeUniformizedMatrix(rateMatrix, statesWithProbabilityGreater0NonPsi, uniformizationRate, exitRates);
                                    
                                    // Compute the vector that is to be added as a compensation for removing the absorbing states.
                                    std::vector<ValueType> b = rateMatrix.getConstrainedRowSumVector(statesWithProbabilityGreater0NonPsi, psiStates);
                                    for (auto& element : b) {
                                        element /= uniformizationRate;
                                    }
                                    
                                    // Start by computing the transient probabilities of reaching a psi state in time t' - t.
                                    std::vector<ValueType> values(statesWithProbabilityGreater0NonPsi.getNumberOfSetBits(), storm::utility::zero<ValueType>());
                                    std::vector<ValueType> subresult = computeTransientProbabilities(env, uniformizedMatrix, &b, upperBound - lowerBound, uniformizationRate, values);
                                    storm::utility::vector::setVectorValues(newSubresult, statesWithProbabilityGreater0NonPsi % relevantStates, subresult);
                                }
                                
                                // Then compute the transient probabilities of being in such a state after t time units. For this,
                                // we must re-uniformize the CTMC, so we need to compute the second uniformized matrix.
                                ValueType uniformizationRate = storm::utility::zero<ValueType>();
                                for (auto const& state : relevantStates) {
                                    uniformizationRate = std::max(uniformizationRate, exitRates[state]);
                                }
                                uniformizationRate *= 1.02;
                                STORM_LOG_THROW(uniformizationRate > 0, storm::exceptions::InvalidStateException, "The uniformization rate must be positive.");
                                
                                // Finally, we compute the second set of transient probabilities.
                                storm::storage::SparseMatrix<ValueType> uniformizedMatrix = computeUniformizedMatrix(rateMatrix, relevantStates, uniformizationRate, exitRates);
                                newSubresult = computeTransientProbabilities<ValueType>(env, uniformizedMatrix, nullptr, lowerBound, uniformizationRate, newSubresult);
                                
                                // Fill in the correct values.
                                result = std::vector<ValueType>(numberOfStates, storm::utility::zero<ValueType>());
                                storm::utility::vector::setVectorValues(result, ~relevantStates, storm::utility::zero<ValueType>());
                                storm::utility::vector::setVectorValues(result, relevantStates, newSubresult);
                            } else {
                                // In this case, the interval is of the form [t, t] with t != 0, t != inf.
                                
                                std::vector<ValueType> newSubresult = std::vector<ValueType>(statesWithProbabilityGreater0.getNumberOfSetBits());
                                storm::utility::vector::setVectorValues(newSubresult, psiStates % statesWithProbabilityGreater0, storm::utility::one<ValueType>());
                                
                                // Then compute the transient probabilities of being in such a state after t time units. For this,
                                // we must re-uniformize the CTMC, so we need to compute the second uniformized matrix.
                                ValueType uniformizationRate = storm::utility::zero<ValueType>();
                                for (auto const& state : statesWithProbabilityGreater0) {
                                    uniformizationRate = std::max(uniformizationRate, exitRates[state]);
                                }
                                uniformizationRate *= 1.02;
                                STORM_LOG_THROW(uniformizationRate > 0, storm::exceptions::InvalidStateException, "The uniformization rate must be positive.");
                                
                                // Finally, we compute the second set of transient probabilities.
                                storm::storage::SparseMatrix<ValueType> uniformizedMatrix = computeUniformizedMatrix(rateMatrix, statesWithProbabilityGreater0, uniformizationRate, exitRates);
                                newSubresult = computeTransientProbabilities<ValueType>(env, uniformizedMatrix, nullptr, lowerBound, uniformizationRate, newSubresult);
                                
                                // Fill in the correct values.
                                result = std::vector<ValueType>(numberOfStates, storm::utility::zero<ValueType>());
                                storm::utility::vector::setVectorValues(result, ~statesWithProbabilityGreater0, storm::utility::zero<ValueType>());
                                storm::utility::vector::setVectorValues(result, statesWithProbabilityGreater0, newSubresult);
                            }
                        }
                    }
                } else {
                    result = std::vector<ValueType>(numberOfStates, storm::utility::zero<ValueType>());
                }
                
                return result;
            }
            
            template <typename ValueType, typename std::enable_if<!storm::NumberTraits<ValueType>::SupportsExponential, int>::type>
            std::vector<ValueType> SparseCtmcCslHelper::computeBoundedUntilProbabilities(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const&, storm::storage::SparseMatrix<ValueType> const&, storm::storage::BitVector const&, storm::storage::BitVector const&, std::vector<ValueType> const&, bool, double, double) {
                STORM_LOG_THROW(false, storm::exceptions::InvalidOperationException, "Computing bounded until probabilities is unsupported for this value type.");
            }

            template <typename ValueType>
            std::vector<ValueType> SparseCtmcCslHelper::computeUntilProbabilities(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::SparseMatrix<ValueType> const& backwardTransitions, std::vector<ValueType> const& exitRateVector, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, bool qualitative) {
                return SparseDtmcPrctlHelper<ValueType>::computeUntilProbabilities(env, std::move(goal), computeProbabilityMatrix(rateMatrix, exitRateVector), backwardTransitions, phiStates, psiStates, qualitative);
            }

            template <typename ValueType>
            std::vector<ValueType> SparseCtmcCslHelper::computeAllUntilProbabilities(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, std::vector<ValueType> const& exitRateVector, storm::storage::BitVector const& initialStates, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates) {
                return SparseDtmcPrctlHelper<ValueType>::computeAllUntilProbabilities(env, std::move(goal), computeProbabilityMatrix(rateMatrix, exitRateVector), initialStates, phiStates, psiStates);
            }

            template <typename ValueType>
            std::vector<ValueType> SparseCtmcCslHelper::computeNextProbabilities(Environment const& env, storm::storage::SparseMatrix<ValueType> const& rateMatrix, std::vector<ValueType> const& exitRateVector, storm::storage::BitVector const& nextStates) {
                return SparseDtmcPrctlHelper<ValueType>::computeNextProbabilities(env, computeProbabilityMatrix(rateMatrix, exitRateVector), nextStates);
            }
            
            template <typename ValueType, typename RewardModelType, typename std::enable_if<storm::NumberTraits<ValueType>::SupportsExponential, int>::type>
            std::vector<ValueType> SparseCtmcCslHelper::computeInstantaneousRewards(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, std::vector<ValueType> const& exitRateVector, RewardModelType const& rewardModel, double timeBound) {
                // Only compute the result if the model has a state-based reward model.
                STORM_LOG_THROW(!rewardModel.empty(), storm::exceptions::InvalidPropertyException, "Missing reward model for formula. Skipping formula.");
                
                uint_fast64_t numberOfStates = rateMatrix.getRowCount();
                
                // Initialize result to state rewards of the this->getModel().
                std::vector<ValueType> result(rewardModel.getStateRewardVector());
                
                // If the time-bound is not zero, we need to perform a transient analysis.
                if (timeBound > 0) {
                    ValueType uniformizationRate = 0;
                    for (auto const& rate : exitRateVector) {
                        uniformizationRate = std::max(uniformizationRate, rate);
                    }
                    uniformizationRate *= 1.02;
                    STORM_LOG_THROW(uniformizationRate > 0, storm::exceptions::InvalidStateException, "The uniformization rate must be positive.");
                    
                    storm::storage::SparseMatrix<ValueType> uniformizedMatrix = computeUniformizedMatrix(rateMatrix, storm::storage::BitVector(numberOfStates, true), uniformizationRate, exitRateVector);
                    result = computeTransientProbabilities<ValueType>(env, uniformizedMatrix, nullptr, timeBound, uniformizationRate, result);
                }
                
                return result;
            }
            
            template <typename ValueType, typename RewardModelType, typename std::enable_if<!storm::NumberTraits<ValueType>::SupportsExponential, int>::type>
            std::vector<ValueType> SparseCtmcCslHelper::computeInstantaneousRewards(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const&, std::vector<ValueType> const&, RewardModelType const&, double) {
                STORM_LOG_THROW(false, storm::exceptions::InvalidOperationException, "Computing instantaneous rewards is unsupported for this value type.");
            }
            
            template <typename ValueType, typename RewardModelType, typename std::enable_if<storm::NumberTraits<ValueType>::SupportsExponential, int>::type>
            std::vector<ValueType> SparseCtmcCslHelper::computeCumulativeRewards(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, std::vector<ValueType> const& exitRateVector, RewardModelType const& rewardModel, double timeBound) {
                // Only compute the result if the model has a state-based reward model.
                STORM_LOG_THROW(!rewardModel.empty(), storm::exceptions::InvalidPropertyException, "Missing reward model for formula. Skipping formula.");
                
                uint_fast64_t numberOfStates = rateMatrix.getRowCount();
                
                // If the time bound is zero, the result is the constant zero vector.
                if (timeBound == 0) {
                    return std::vector<ValueType>(numberOfStates, storm::utility::zero<ValueType>());
                }
                
                // Otherwise, we need to perform some computations.
                
                // Start with the uniformization.
                ValueType uniformizationRate = 0;
                for (auto const& rate : exitRateVector) {
                    uniformizationRate = std::max(uniformizationRate, rate);
                }
                uniformizationRate *= 1.02;
                STORM_LOG_THROW(uniformizationRate > 0, storm::exceptions::InvalidStateException, "The uniformization rate must be positive.");
                
                storm::storage::SparseMatrix<ValueType> uniformizedMatrix = computeUniformizedMatrix(rateMatrix, storm::storage::BitVector(numberOfStates, true), uniformizationRate, exitRateVector);
                
                // Compute the total state reward vector.
                std::vector<ValueType> totalRewardVector = rewardModel.getTotalRewardVector(rateMatrix, exitRateVector);
                
                // Finally, compute the transient probabilities.
                return computeTransientProbabilities<ValueType, true>(env, uniformizedMatrix, nullptr, timeBound, uniformizationRate, totalRewardVector);
            }
            
            template <typename ValueType, typename RewardModelType, typename std::enable_if<!storm::NumberTraits<ValueType>::SupportsExponential, int>::type>
            std::vector<ValueType> SparseCtmcCslHelper::computeCumulativeRewards(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const&, std::vector<ValueType> const&, RewardModelType const&, double) {
                STORM_LOG_THROW(false, storm::exceptions::InvalidOperationException, "Computing cumulative rewards is unsupported for this value type.");
            }
            
            template <typename ValueType>
            std::vector<ValueType> SparseCtmcCslHelper::computeReachabilityTimes(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::SparseMatrix<ValueType> const& backwardTransitions, std::vector<ValueType> const& exitRateVector, storm::storage::BitVector const& targetStates, bool qualitative) {
                // Compute expected time on CTMC by reduction to DTMC with rewards.
                storm::storage::SparseMatrix<ValueType> probabilityMatrix = computeProbabilityMatrix(rateMatrix, exitRateVector);
                
                // Initialize rewards.
                std::vector<ValueType> totalRewardVector;
                for (size_t i = 0; i < exitRateVector.size(); ++i) {
                    if (targetStates[i] || storm::utility::isZero(exitRateVector[i])) {
                        // Set reward for target states or states without outgoing transitions to 0.
                        totalRewardVector.push_back(storm::utility::zero<ValueType>());
                    } else {
                        // Reward is (1 / exitRate).
                        totalRewardVector.push_back(storm::utility::one<ValueType>() / exitRateVector[i]);
                    }
                }
                
                return storm::modelchecker::helper::SparseDtmcPrctlHelper<ValueType>::computeReachabilityRewards(env, std::move(goal), probabilityMatrix, backwardTransitions, totalRewardVector, targetStates, qualitative);
            }

            template <typename ValueType, typename RewardModelType>
            std::vector<ValueType> SparseCtmcCslHelper::computeReachabilityRewards(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::SparseMatrix<ValueType> const& backwardTransitions, std::vector<ValueType> const& exitRateVector, RewardModelType const& rewardModel, storm::storage::BitVector const& targetStates, bool qualitative) {
                STORM_LOG_THROW(!rewardModel.empty(), storm::exceptions::InvalidPropertyException, "Missing reward model for formula. Skipping formula.");
                
                storm::storage::SparseMatrix<ValueType> probabilityMatrix = computeProbabilityMatrix(rateMatrix, exitRateVector);
                
                std::vector<ValueType> totalRewardVector;
                if (rewardModel.hasStateRewards()) {
                    totalRewardVector = rewardModel.getStateRewardVector();
                    typename std::vector<ValueType>::const_iterator it2 = exitRateVector.begin();
                    for (typename std::vector<ValueType>::iterator it1 = totalRewardVector.begin(), ite1 = totalRewardVector.end(); it1 != ite1; ++it1, ++it2) {
                        *it1 /= *it2;
                    }
                    if (rewardModel.hasStateActionRewards()) {
                        storm::utility::vector::addVectors(totalRewardVector, rewardModel.getStateActionRewardVector(), totalRewardVector);
                    }
                    if (rewardModel.hasTransitionRewards()) {
                        storm::utility::vector::addVectors(totalRewardVector, probabilityMatrix.getPointwiseProductRowSumVector(rewardModel.getTransitionRewardMatrix()), totalRewardVector);
                    }
                } else if (rewardModel.hasTransitionRewards()) {
                    totalRewardVector = probabilityMatrix.getPointwiseProductRowSumVector(rewardModel.getTransitionRewardMatrix());
                    if (rewardModel.hasStateActionRewards()) {
                        storm::utility::vector::addVectors(totalRewardVector, rewardModel.getStateActionRewardVector(), totalRewardVector);
                    }
                } else {
                    totalRewardVector = rewardModel.getStateActionRewardVector();
                }
                
                return storm::modelchecker::helper::SparseDtmcPrctlHelper<ValueType>::computeReachabilityRewards(env, std::move(goal), probabilityMatrix, backwardTransitions, totalRewardVector, targetStates, qualitative);
            }
            
            template <typename ValueType, typename RewardModelType>
            std::vector<ValueType> SparseCtmcCslHelper::computeTotalRewards(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::SparseMatrix<ValueType> const& backwardTransitions, std::vector<ValueType> const& exitRateVector, RewardModelType const& rewardModel, bool qualitative) {
                STORM_LOG_THROW(!rewardModel.empty(), storm::exceptions::InvalidPropertyException, "Missing reward model for formula. Skipping formula.");
                
                storm::storage::SparseMatrix<ValueType> probabilityMatrix = computeProbabilityMatrix(rateMatrix, exitRateVector);
                
                std::vector<ValueType> totalRewardVector;
                if (rewardModel.hasStateRewards()) {
                    totalRewardVector = rewardModel.getStateRewardVector();
                    typename std::vector<ValueType>::const_iterator it2 = exitRateVector.begin();
                    for (typename std::vector<ValueType>::iterator it1 = totalRewardVector.begin(), ite1 = totalRewardVector.end(); it1 != ite1; ++it1, ++it2) {
                        *it1 /= *it2;
                    }
                    if (rewardModel.hasStateActionRewards()) {
                        storm::utility::vector::addVectors(totalRewardVector, rewardModel.getStateActionRewardVector(), totalRewardVector);
                    }
                    if (rewardModel.hasTransitionRewards()) {
                        storm::utility::vector::addVectors(totalRewardVector, probabilityMatrix.getPointwiseProductRowSumVector(rewardModel.getTransitionRewardMatrix()), totalRewardVector);
                    }
                } else if (rewardModel.hasTransitionRewards()) {
                    totalRewardVector = probabilityMatrix.getPointwiseProductRowSumVector(rewardModel.getTransitionRewardMatrix());
                    if (rewardModel.hasStateActionRewards()) {
                        storm::utility::vector::addVectors(totalRewardVector, rewardModel.getStateActionRewardVector(), totalRewardVector);
                    }
                } else {
                    totalRewardVector = rewardModel.getStateActionRewardVector();
                }
                
                RewardModelType dtmcRewardModel(std::move(totalRewardVector));
                return storm::modelchecker::helper::SparseDtmcPrctlHelper<ValueType>::computeTotalRewards(env, std::move(goal), probabilityMatrix, backwardTransitions, dtmcRewardModel, qualitative);
            }
            
            template <typename ValueType>
            std::vector<ValueType> SparseCtmcCslHelper::computeLongRunAverageProbabilities(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::BitVector const& psiStates, std::vector<ValueType> const* exitRateVector) {
                
                // If there are no goal states, we avoid the computation and directly return zero.
                uint_fast64_t numberOfStates = rateMatrix.getRowCount();
                if (psiStates.empty()) {
                    return std::vector<ValueType>(numberOfStates, storm::utility::zero<ValueType>());
                }
                
                // Likewise, if all bits are set, we can avoid the computation.
                if (psiStates.full()) {
                    return std::vector<ValueType>(numberOfStates, storm::utility::one<ValueType>());
                }
                
                ValueType zero = storm::utility::zero<ValueType>();
                ValueType one = storm::utility::one<ValueType>();
                
                return computeLongRunAverages<ValueType>(env, std::move(goal), rateMatrix,
                                              [&zero, &one, &psiStates] (storm::storage::sparse::state_type const& state) -> ValueType {
                                                  if (psiStates.get(state)) {
                                                      return one;
                                                  }
                                                  return zero;
                                              },
                                              exitRateVector);
            }
            
            template <typename ValueType, typename RewardModelType>
            std::vector<ValueType> SparseCtmcCslHelper::computeLongRunAverageRewards(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, RewardModelType const& rewardModel, std::vector<ValueType> const* exitRateVector) {
                // Only compute the result if the model has a state-based reward model.
                STORM_LOG_THROW(!rewardModel.empty(), storm::exceptions::InvalidPropertyException, "Missing reward model for formula. Skipping formula.");

                return computeLongRunAverages<ValueType>(env, std::move(goal), rateMatrix,
                        [&] (storm::storage::sparse::state_type const& state) -> ValueType {
                            ValueType result = rewardModel.hasStateRewards() ? rewardModel.getStateReward(state) : storm::utility::zero<ValueType>();
                            if (rewardModel.hasStateActionRewards()) {
                                // State action rewards are multiplied with the exit rate r(s). Then, multiplying the reward with the expected time we stay at s (i.e. 1/r(s)) yields the original state reward
                                if (exitRateVector) {
                                    result += rewardModel.getStateActionReward(state) * (*exitRateVector)[state];
                                } else {
                                    result += rewardModel.getStateActionReward(state);
                                }
                            }
                            if (rewardModel.hasTransitionRewards()) {
                                // Transition rewards are already multiplied with the rates
                                result += rateMatrix.getPointwiseProductRowSum(rewardModel.getTransitionRewardMatrix(), state);
                            }
                            return result;
                        },
                        exitRateVector);
            }
            
            template <typename ValueType>
            std::vector<ValueType> SparseCtmcCslHelper::computeLongRunAverageRewards(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, std::vector<ValueType> const& stateRewardVector, std::vector<ValueType> const* exitRateVector) {
                return computeLongRunAverages<ValueType>(env, std::move(goal), rateMatrix,
                                                         [&stateRewardVector] (storm::storage::sparse::state_type const& state) -> ValueType {
                                                             return stateRewardVector[state];
                                                         },
                                                         exitRateVector);
            }
            
            template <typename ValueType>
            std::vector<ValueType> SparseCtmcCslHelper::computeLongRunAverages(Environment const& env, storm::solver::SolveGoal<ValueType>&& goal, storm::storage::SparseMatrix<ValueType> const& rateMatrix, std::function<ValueType (storm::storage::sparse::state_type const& state)> const& valueGetter, std::vector<ValueType> const* exitRateVector){
                storm::storage::SparseMatrix<ValueType> probabilityMatrix;
                if (exitRateVector) {
                    probabilityMatrix = computeProbabilityMatrix(rateMatrix, *exitRateVector);
                } else {
                    probabilityMatrix = rateMatrix;
                }
                uint_fast64_t numberOfStates = rateMatrix.getRowCount();
            
                // Start by decomposing the CTMC into its BSCCs.
                storm::storage::StronglyConnectedComponentDecomposition<ValueType> bsccDecomposition(rateMatrix, storm::storage::StronglyConnectedComponentDecompositionOptions().onlyBottomSccs());
                
                STORM_LOG_DEBUG("Found " << bsccDecomposition.size() << " BSCCs.");

                // Prepare the vector holding the LRA values for each of the BSCCs.
                std::vector<ValueType> bsccLra;
                bsccLra.reserve(bsccDecomposition.size());
                
                // Keep track of the maximal and minimal value occuring in one of the BSCCs
                ValueType maxValue, minValue;
                storm::storage::BitVector statesInBsccs(numberOfStates);
                auto backwardTransitions = rateMatrix.transpose();
                for (auto const& bscc : bsccDecomposition) {
                    for (auto const& state : bscc) {
                        statesInBsccs.set(state);
                    }
                    bsccLra.push_back(computeLongRunAveragesForBscc<ValueType>(env, bscc, rateMatrix, backwardTransitions, valueGetter, exitRateVector));
                    if (bsccLra.size() == 1) {
                        maxValue = bsccLra.back();
                        minValue = bsccLra.back();
                    } else {
                        maxValue = std::max(bsccLra.back(), maxValue);
                        minValue = std::min(bsccLra.back(), minValue);
                    }
                }
                
                storm::storage::BitVector statesNotInBsccs = ~statesInBsccs;
                STORM_LOG_DEBUG("Found " << statesInBsccs.getNumberOfSetBits() << " states in BSCCs.");
                
                std::vector<uint64_t> stateToBsccMap(statesInBsccs.size(), -1);
                for (uint64_t bsccIndex = 0; bsccIndex < bsccDecomposition.size(); ++bsccIndex) {
                    for (auto const& state : bsccDecomposition[bsccIndex]) {
                        stateToBsccMap[state] = bsccIndex;
                    }
                }
                
                std::vector<ValueType> rewardSolution;
                if (!statesNotInBsccs.empty()) {
                    // Calculate LRA for states not in bsccs as expected reachability rewards.
                    // Target states are states in bsccs, transition reward is the lra of the bscc for each transition into a
                    // bscc and 0 otherwise. This corresponds to the sum of LRAs in BSCC weighted by the reachability probability
                    // of the BSCC.
                    
                    std::vector<ValueType> rewardRightSide;
                    rewardRightSide.reserve(statesNotInBsccs.getNumberOfSetBits());
                    
                    for (auto state : statesNotInBsccs) {
                        ValueType reward = storm::utility::zero<ValueType>();
                        for (auto entry : rateMatrix.getRow(state)) {
                            if (statesInBsccs.get(entry.getColumn())) {
                                if (exitRateVector) {
                                    reward += (entry.getValue() / (*exitRateVector)[state]) * bsccLra[stateToBsccMap[entry.getColumn()]];
                                } else {
                                    reward += entry.getValue() * bsccLra[stateToBsccMap[entry.getColumn()]];
                                }
                            }
                        }
                        rewardRightSide.push_back(reward);
                    }
                    
                    // Compute reachability rewards
                    storm::solver::GeneralLinearEquationSolverFactory<ValueType> linearEquationSolverFactory;
                    bool isEqSysFormat = linearEquationSolverFactory.getEquationProblemFormat(env) == storm::solver::LinearEquationSolverProblemFormat::EquationSystem;
                    storm::storage::SparseMatrix<ValueType> rewardEquationSystemMatrix = rateMatrix.getSubmatrix(false, statesNotInBsccs, statesNotInBsccs, isEqSysFormat);
                    if (exitRateVector) {
                        uint64_t localRow = 0;
                        for (auto const& globalRow : statesNotInBsccs) {
                            for (auto& entry : rewardEquationSystemMatrix.getRow(localRow)) {
                                entry.setValue(entry.getValue() / (*exitRateVector)[globalRow]);
                            }
                            ++localRow;
                        }
                    }
                    if (isEqSysFormat) {
                        rewardEquationSystemMatrix.convertToEquationSystem();
                    }
                    rewardSolution = std::vector<ValueType>(rewardEquationSystemMatrix.getColumnCount(), (maxValue + minValue) / storm::utility::convertNumber<ValueType,uint64_t>(2));
                   // std::cout << rewardEquationSystemMatrix << std::endl;
                   // std::cout << storm::utility::vector::toString(rewardRightSide) << std::endl;
                    std::unique_ptr<storm::solver::LinearEquationSolver<ValueType>> solver = linearEquationSolverFactory.create(env, std::move(rewardEquationSystemMatrix));
                    solver->setBounds(minValue, maxValue);
                    // Check solver requirements
                    auto requirements = solver->getRequirements(env);
                    STORM_LOG_THROW(!requirements.hasEnabledCriticalRequirement(), storm::exceptions::UncheckedRequirementException, "Solver requirements " + requirements.getEnabledRequirementsAsString() + " not checked.");
                    solver->solveEquations(env, rewardSolution, rewardRightSide);
                }
                
                // Fill the result vector.
                std::vector<ValueType> result(numberOfStates);
                auto rewardSolutionIter = rewardSolution.begin();
                
                for (uint_fast64_t bsccIndex = 0; bsccIndex < bsccDecomposition.size(); ++bsccIndex) {
                    storm::storage::StronglyConnectedComponent const& bscc = bsccDecomposition[bsccIndex];
                    
                    for (auto const& state : bscc) {
                        result[state] = bsccLra[bsccIndex];
                    }
                }
                for (auto state : statesNotInBsccs) {
                    STORM_LOG_ASSERT(rewardSolutionIter != rewardSolution.end(), "Too few elements in solution.");
                    // Take the value from the reward computation. Since the n-th state not in any bscc is the n-th
                    // entry in rewardSolution we can just take the next value from the iterator.
                    result[state] = *rewardSolutionIter;
                    ++rewardSolutionIter;
                }
                
                return result;
            }

            template <typename ValueType>
            ValueType SparseCtmcCslHelper::computeLongRunAveragesForBscc(Environment const& env, storm::storage::StronglyConnectedComponent const& bscc, storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::SparseMatrix<ValueType> const& backwardTransitions, std::function<ValueType (storm::storage::sparse::state_type const& state)> const& valueGetter, std::vector<ValueType> const* exitRateVector) {
                std::cout << std::endl << "========== BSCC =======" << std::endl;
                storm::storage::BitVector bsccStates(rateMatrix.getRowCount());
                for (auto const& s : bscc) { bsccStates.set(s); std::cout << s << ": " << valueGetter(s) << "\t";}
                auto subm = rateMatrix.getSubmatrix(false,bsccStates,bsccStates);
                std::cout << std::endl << subm << std::endl;
                
                std::cout << std::endl << "========== EQSYS =======" << std::endl;

                // Catch the trivial case for bsccs of size 1
                if (bscc.size() == 1) {
                    std::cout << std::endl << "========== Result = " << valueGetter(*bscc.begin()) << " =======" << std::endl;

                    return valueGetter(*bscc.begin());
                }
                
                return computeLongRunAveragesForBsccVi(env, bscc, rateMatrix, backwardTransitions, valueGetter, exitRateVector);
                return computeLongRunAveragesForBsccEqSys(env, bscc, rateMatrix, backwardTransitions, valueGetter, exitRateVector);
            }
            
            template <>
            storm::RationalFunction SparseCtmcCslHelper::computeLongRunAveragesForBsccVi<storm::RationalFunction>(Environment const&, storm::storage::StronglyConnectedComponent const&, storm::storage::SparseMatrix<storm::RationalFunction> const&, storm::storage::SparseMatrix<storm::RationalFunction> const&, std::function<storm::RationalFunction (storm::storage::sparse::state_type const& state)> const&, std::vector<storm::RationalFunction> const*) {
                STORM_LOG_THROW(false, storm::exceptions::NotSupportedException, "The requested Method for LRA computation is not supported for parametric models.");
            }
                
            template <typename ValueType>
            ValueType SparseCtmcCslHelper::computeLongRunAveragesForBsccVi(Environment const& env, storm::storage::StronglyConnectedComponent const& bscc, storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::SparseMatrix<ValueType> const& backwardTransitions, std::function<ValueType (storm::storage::sparse::state_type const& state)> const& valueGetter, std::vector<ValueType> const* exitRateVector) {
                
                // Initialize data about the bscc
                storm::storage::BitVector bsccStates(rateMatrix.getRowGroupCount(), false);
                for (auto const& state : bscc) {
                    bsccStates.set(state);
                }
                
                // Get the uniformization rate
                ValueType uniformizationRate = storm::utility::one<ValueType>();
                if (exitRateVector) {
                    uniformizationRate = storm::utility::vector::max_if(*exitRateVector, bsccStates);
                }
                // To ensure that the model is aperiodic, we need to make sure that every Markovian state gets a self loop.
                // Hence, we increase the uniformization rate a little.
                uniformizationRate += storm::utility::one<ValueType>(); // Todo: try other values such as *=1.01

                // Get the transitions of the submodel
                typename storm::storage::SparseMatrix<ValueType> bsccMatrix = rateMatrix.getSubmatrix(true, bsccStates, bsccStates, true);
                
                // Uniformize the transitions
                uint64_t subState = 0;
                for (auto state : bsccStates) {
                    for (auto& entry : bsccMatrix.getRow(subState)) {
                        if (entry.getColumn() == subState) {
                            if (exitRateVector) {
                                entry.setValue(storm::utility::one<ValueType>() + (entry.getValue() - (*exitRateVector)[state]) / uniformizationRate);
                            } else {
                                entry.setValue(storm::utility::one<ValueType>() + (entry.getValue() - storm::utility::one<ValueType>()) / uniformizationRate);
                            }
                        } else {
                            entry.setValue(entry.getValue() / uniformizationRate);
                        }
                    }
                    ++subState;
                }

                // Compute the rewards obtained in a single uniformization step
                std::vector<ValueType> markovianRewards;
                markovianRewards.reserve(bsccMatrix.getRowCount());
                for (auto const& state : bsccStates) {
                    ValueType stateRewardScalingFactor = storm::utility::one<ValueType>() / uniformizationRate;
                    // ValueType actionRewardScalingFactor = exitRateVector[state] / uniformizationRate; // action rewards are currently not supported
                    markovianRewards.push_back(valueGetter(state) * stateRewardScalingFactor);
                }
                
                // start the iterations
                ValueType precision = storm::utility::convertNumber<ValueType>(1e-6) / uniformizationRate; // TODO env.solver().minMax().getPrecision()) / uniformizationRate;
                bool relative = true; // TODO env.solver().minMax().getRelativeTerminationCriterion();
                std::vector<ValueType> v(bsccMatrix.getRowCount(), storm::utility::zero<ValueType>());
                std::vector<ValueType> w = v;
                std::unique_ptr<storm::solver::MinMaxLinearEquationSolver<ValueType>> solver;
                while (true) {
                    // Compute the values for the markovian states. We also keep track of the maximal and minimal difference between two values (for convergence checking)
                    auto vIt = v.begin();
                    uint64_t row = 0;
                    ValueType newValue = markovianRewards[row] + bsccMatrix.multiplyRowWithVector(row, w);
                    ValueType maxDiff = newValue - *vIt;
                    ValueType minDiff = maxDiff;
                    *vIt = newValue;
                    for (++vIt, ++row; row < bsccMatrix.getRowCount(); ++vIt, ++row) {
                        newValue = markovianRewards[row] + bsccMatrix.multiplyRowWithVector(row, w);
                        ValueType diff = newValue - *vIt;
                        maxDiff = std::max(maxDiff, diff);
                        minDiff = std::min(minDiff, diff);
                        *vIt = newValue;
                    }

                    // Check for convergence
                    if ((maxDiff - minDiff) <= (relative ? (precision * (v.front() + minDiff)) : precision)) {
                        break;
                    }
                    
                    // update the rhs of the equation system
                    ValueType referenceValue = v.front();
                    storm::utility::vector::applyPointwise<ValueType, ValueType>(v, w, [&referenceValue] (ValueType const& v_i) -> ValueType { return v_i - referenceValue; });
                }
                std::cout << std::endl << "========== VI Result = " << v.front() * uniformizationRate << " =======" << std::endl;
                return v.front() * uniformizationRate;
            }
            
            template <typename ValueType>
            ValueType SparseCtmcCslHelper::computeLongRunAveragesForBsccEqSys(Environment const& env, storm::storage::StronglyConnectedComponent const& bscc, storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::SparseMatrix<ValueType> const& backwardTransitions, std::function<ValueType (storm::storage::sparse::state_type const& state)> const& valueGetter, std::vector<ValueType> const* exitRateVector) {
                
                // Get a mapping from global state indices to local ones.
                std::unordered_map<uint64_t, uint64_t> toLocalIndexMap;
                uint64_t localIndex = 0;
                for (auto const& globalIndex : bscc) {
                    toLocalIndexMap[globalIndex] = localIndex;
                    ++localIndex;
                }
                
                // Build the equation system matrix
                
                
                // Build the equation system matrix for A[s,s] =  R(s,s) - r(s) & A[s,s'] = R(s,s') (where s != s')
                // x_0+...+x_n=1 & x*A=0  <=>  x_0+...+x_n=1 & A^t*x=0 [ <=> 1-x_1+...+x_n=x_0 & (1-A^t)*x = x ]
                storm::solver::GeneralLinearEquationSolverFactory<ValueType> linearEquationSolverFactory;
                storm::storage::SparseMatrixBuilder<ValueType> builder(bscc.size(), bscc.size());
                bool isEquationSystemFormat = linearEquationSolverFactory.getEquationProblemFormat(env) == storm::solver::LinearEquationSolverProblemFormat::EquationSystem;
                // The first row asserts that the values sum up to one
                uint64_t row = 0;
                if (isEquationSystemFormat) {
                    for (uint64_t state = 0; state < bscc.size(); ++state) {
                            builder.addNextValue(row, state, storm::utility::one<ValueType>());
                    }
                } else {
                    for (uint64_t state = 1; state < bscc.size(); ++state) {
                            builder.addNextValue(row, state, -storm::utility::one<ValueType>());
                    }
                }
                ++row;
                // Build the equation system matrix
                // We can skip the first state to make the equation system matrix square
                auto stateIt = bscc.begin();
                for (++stateIt; stateIt != bscc.end(); ++stateIt) {
                    ValueType diagonalValue = (exitRateVector == nullptr) ? -storm::utility::one<ValueType>() : -(*exitRateVector)[*stateIt];
                    if (!isEquationSystemFormat) {
                        diagonalValue = storm::utility::one<ValueType>() - diagonalValue;
                    }
                    bool insertedDiagonal = storm::utility::isZero(diagonalValue);
                    for (auto const& backwardsEntry : backwardTransitions.getRow(*stateIt)) {
                        auto localIndexIt = toLocalIndexMap.find(backwardsEntry.getColumn());
                        if (localIndexIt != toLocalIndexMap.end()) {
                            ValueType val = backwardsEntry.getValue();
                            if (!isEquationSystemFormat) {
                                val = -val;
                            }
                            uint64_t localIndex = localIndexIt->second;
                            if (!insertedDiagonal && localIndex == row) {
                                builder.addNextValue(row, localIndex, val + diagonalValue);
                                insertedDiagonal = true;
                            } else {
                                if (!insertedDiagonal && localIndex > row) {
                                    builder.addNextValue(row, row, diagonalValue);
                                    insertedDiagonal = true;
                                }
                                builder.addNextValue(row, localIndex, val);
                            }
                        }
                    }
                    if (!insertedDiagonal) {
                        builder.addNextValue(row, row, diagonalValue);
                    }
                    ++row;
                }

                // Create a linear equation solver
                auto matrix = builder.build();
                std::cout << matrix << std::endl;
                auto solver = linearEquationSolverFactory.create(env, std::move(matrix));
                solver->setBounds(storm::utility::zero<ValueType>(), storm::utility::one<ValueType>());
                // Check solver requirements.
                auto requirements = solver->getRequirements(env);
                STORM_LOG_THROW(!requirements.hasEnabledCriticalRequirement(), storm::exceptions::UncheckedRequirementException, "Solver requirements " + requirements.getEnabledRequirementsAsString() + " not checked.");
                
                std::vector<ValueType> bsccEquationSystemRightSide(bscc.size(), storm::utility::zero<ValueType>());
                bsccEquationSystemRightSide.front() = storm::utility::one<ValueType>();
                std::vector<ValueType> bsccEquationSystemSolution(bscc.size(), storm::utility::one<ValueType>() / storm::utility::convertNumber<ValueType, uint64_t>(bscc.size()));
                std::cout << storm::utility::vector::toString(bsccEquationSystemRightSide) << std::endl;
                solver->solveEquations(env, bsccEquationSystemSolution, bsccEquationSystemRightSide);
                std::cout << storm::utility::vector::toString(bsccEquationSystemSolution) << std::endl;
                // If exit rates were given, we need to 'fix' the results to also account for the timing behaviour.
                if (false && exitRateVector != nullptr) {
                    ValueType totalValue = storm::utility::zero<ValueType>();
                    auto solIt = bsccEquationSystemSolution.begin();
                    for (auto const& globalState : bscc) {
                        totalValue += (*solIt) * (storm::utility::one<ValueType>() / (*exitRateVector)[globalState]);
                        ++solIt;
                    }
                    assert(solIt == bsccEquationSystemSolution.end());
                    solIt = bsccEquationSystemSolution.begin();
                    for (auto const& globalState : bscc) {
                        *solIt = ((*solIt) * (storm::utility::one<ValueType>() / (*exitRateVector)[globalState])) / totalValue;
                        ++solIt;
                    }
                    assert(solIt == bsccEquationSystemSolution.end());
                }
                
                // Calculate final LRA Value
                ValueType result = storm::utility::zero<ValueType>();
                auto solIt = bsccEquationSystemSolution.begin();
                for (auto const& globalState : bscc) {
                    result += valueGetter(globalState) * (*solIt);
                    ++solIt;
                }
                assert(solIt == bsccEquationSystemSolution.end());
                std::cout << std::endl << "========== Result = " << result << " =======" << std::endl;

                return result;
            }
            
            template <typename ValueType, typename std::enable_if<storm::NumberTraits<ValueType>::SupportsExponential, int>::type>
            std::vector<ValueType> SparseCtmcCslHelper::computeAllTransientProbabilities(Environment const& env, storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::BitVector const& initialStates, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, std::vector<ValueType> const& exitRates, double timeBound) {

                // Compute transient probabilities going from initial state
                // Instead of y=Px we now compute y=xP <=> y^T=P^Tx^T via transposition
                uint_fast64_t numberOfStates = rateMatrix.getRowCount();

                // Create the result vector.
                std::vector<ValueType> result = std::vector<ValueType>(numberOfStates, storm::utility::zero<ValueType>());

                storm::storage::SparseMatrix<ValueType> transposedMatrix(rateMatrix);
                transposedMatrix.makeRowsAbsorbing(psiStates);
                std::vector<ValueType> newRates = exitRates;
                for (auto const& state : psiStates) {
                    newRates[state] = storm::utility::one<ValueType>();
                }

                // Identify all maybe states which have a probability greater than 0 to be reached from the initial state.
                //storm::storage::BitVector statesWithProbabilityGreater0 = storm::utility::graph::performProbGreater0(transposedMatrix, phiStates, initialStates);
                //STORM_LOG_INFO("Found " << statesWithProbabilityGreater0.getNumberOfSetBits() << " states with probability greater 0.");

                //storm::storage::BitVector relevantStates = statesWithProbabilityGreater0 & ~initialStates;//phiStates | psiStates;
                storm::storage::BitVector relevantStates(numberOfStates, true);
                STORM_LOG_DEBUG(relevantStates.getNumberOfSetBits() << " relevant states.");

                if (!relevantStates.empty()) {
                    // Find the maximal rate of all relevant states to take it as the uniformization rate.
                    ValueType uniformizationRate = 0;
                    for (auto const& state : relevantStates) {
                        uniformizationRate = std::max(uniformizationRate, newRates[state]);
                    }
                    uniformizationRate *= 1.02;
                    STORM_LOG_THROW(uniformizationRate > 0, storm::exceptions::InvalidStateException, "The uniformization rate must be positive.");

                    transposedMatrix = transposedMatrix.transpose();

                    // Compute the uniformized matrix.
                    storm::storage::SparseMatrix<ValueType> uniformizedMatrix = computeUniformizedMatrix(transposedMatrix, relevantStates, uniformizationRate, newRates);

                    // Compute the vector that is to be added as a compensation for removing the absorbing states.
                    /*std::vector<ValueType> b = transposedMatrix.getConstrainedRowSumVector(relevantStates, initialStates);
                    for (auto& element : b) {
                        element /= uniformizationRate;
                        std::cout << element << std::endl;
                    }*/

                    std::vector<ValueType> values(relevantStates.getNumberOfSetBits(), storm::utility::zero<ValueType>());
                    // Set initial states
                    size_t i = 0;
                    ValueType initDist = storm::utility::one<ValueType>() / initialStates.getNumberOfSetBits();
                    for (auto const& state : relevantStates) {
                        if (initialStates.get(state)) {
                            values[i] = initDist;
                        }
                        ++i;
                    }
                    // Finally compute the transient probabilities.
                    std::vector<ValueType> subresult = computeTransientProbabilities<ValueType>(env, uniformizedMatrix, nullptr, timeBound, uniformizationRate, values);

                    storm::utility::vector::setVectorValues(result, relevantStates, subresult);
                }

                return result;
            }

            template <typename ValueType, typename std::enable_if<!storm::NumberTraits<ValueType>::SupportsExponential, int>::type>
            std::vector<ValueType> SparseCtmcCslHelper::computeAllTransientProbabilities(Environment const&, storm::storage::SparseMatrix<ValueType> const&, storm::storage::BitVector const&, storm::storage::BitVector const&, storm::storage::BitVector const&, std::vector<ValueType> const&, double) {
                STORM_LOG_THROW(false, storm::exceptions::InvalidOperationException, "Computing bounded until probabilities is unsupported for this value type.");
            }

            template <typename ValueType, typename std::enable_if<storm::NumberTraits<ValueType>::SupportsExponential, int>::type>
            storm::storage::SparseMatrix<ValueType> SparseCtmcCslHelper::computeUniformizedMatrix(storm::storage::SparseMatrix<ValueType> const& rateMatrix, storm::storage::BitVector const& maybeStates, ValueType uniformizationRate, std::vector<ValueType> const& exitRates) {
                STORM_LOG_DEBUG("Computing uniformized matrix using uniformization rate " << uniformizationRate << ".");
                STORM_LOG_DEBUG("Keeping " << maybeStates.getNumberOfSetBits() << " rows.");
                
                // Create the submatrix that only contains the states with a positive probability (including the
                // psi states) and reserve space for elements on the diagonal.
                storm::storage::SparseMatrix<ValueType> uniformizedMatrix = rateMatrix.getSubmatrix(false, maybeStates, maybeStates, true);
                
                // Now we need to perform the actual uniformization. That is, all entries need to be divided by
                // the uniformization rate, and the diagonal needs to be set to the negative exit rate of the
                // state plus the self-loop rate and then increased by one.
                uint_fast64_t currentRow = 0;
                for (auto const& state : maybeStates) {
                    for (auto& element : uniformizedMatrix.getRow(currentRow)) {
                        if (element.getColumn() == currentRow) {
                            element.setValue((element.getValue() - exitRates[state]) / uniformizationRate + storm::utility::one<ValueType>());
                        } else {
                            element.setValue(element.getValue() / uniformizationRate);
                        }
                    }
                    ++currentRow;
                }
                
                return uniformizedMatrix;
            }

            template<typename ValueType, bool useMixedPoissonProbabilities, typename std::enable_if<storm::NumberTraits<ValueType>::SupportsExponential, int>::type>
            std::vector<ValueType> SparseCtmcCslHelper::computeTransientProbabilities(Environment const& env, storm::storage::SparseMatrix<ValueType> const& uniformizedMatrix, std::vector<ValueType> const* addVector, ValueType timeBound, ValueType uniformizationRate, std::vector<ValueType> values) {
                
                ValueType lambda = timeBound * uniformizationRate;
                
                // If no time can pass, the current values are the result.
                if (storm::utility::isZero(lambda)) {
                    return values;
                }
                
                // Use Fox-Glynn to get the truncation points and the weights.
//                std::tuple<uint_fast64_t, uint_fast64_t, ValueType, std::vector<ValueType>> foxGlynnResult = storm::utility::numerical::getFoxGlynnCutoff(lambda, 1e+300, storm::settings::getModule<storm::settings::modules::GeneralSettings>().getPrecision() / 8.0);
                
                storm::utility::numerical::FoxGlynnResult<ValueType> foxGlynnResult = storm::utility::numerical::foxGlynn(lambda, storm::settings::getModule<storm::settings::modules::GeneralSettings>().getPrecision() / 8.0);
                STORM_LOG_DEBUG("Fox-Glynn cutoff points: left=" << foxGlynnResult.left << ", right=" << foxGlynnResult.right);
                
                // Scale the weights so they add up to one.
                for (auto& element : foxGlynnResult.weights) {
                    element /= foxGlynnResult.totalWeight;
                }
                
                // If the cumulative reward is to be computed, we need to adjust the weights.
                if (useMixedPoissonProbabilities) {
                    ValueType sum = storm::utility::zero<ValueType>();
                    
                    for (auto& element : foxGlynnResult.weights) {
                        sum += element;
                        element = (1 - sum) / uniformizationRate;
                    }
                }
                
                STORM_LOG_DEBUG("Starting iterations with " << uniformizedMatrix.getRowCount() << " x " << uniformizedMatrix.getColumnCount() << " matrix.");
                
                // Initialize result.
                std::vector<ValueType> result;
                uint_fast64_t startingIteration = foxGlynnResult.left;
                if (startingIteration == 0) {
                    result = values;
                    storm::utility::vector::scaleVectorInPlace(result, foxGlynnResult.weights.front());
                    ++startingIteration;
                } else {
                    if (useMixedPoissonProbabilities) {
                        result = std::vector<ValueType>(values.size());
                        std::function<ValueType (ValueType const&)> scaleWithUniformizationRate = [&uniformizationRate] (ValueType const& a) -> ValueType { return a / uniformizationRate; };
                        storm::utility::vector::applyPointwise(values, result, scaleWithUniformizationRate);
                    } else {
                        result = std::vector<ValueType>(values.size());
                    }
                }
                
                auto multiplier = storm::solver::MultiplierFactory<ValueType>().create(env, uniformizedMatrix);
                if (!useMixedPoissonProbabilities && foxGlynnResult.left > 1) {
                    // Perform the matrix-vector multiplications (without adding).
                    multiplier->repeatedMultiply(env, values, addVector, foxGlynnResult.left - 1);
                } else if (useMixedPoissonProbabilities) {
                    std::function<ValueType(ValueType const&, ValueType const&)> addAndScale = [&uniformizationRate] (ValueType const& a, ValueType const& b) { return a + b / uniformizationRate; };
                    
                    // For the iterations below the left truncation point, we need to add and scale the result with the uniformization rate.
                    for (uint_fast64_t index = 1; index < startingIteration; ++index) {
                        multiplier->multiply(env, values, nullptr, values);
                        storm::utility::vector::applyPointwise(result, values, result, addAndScale);
                    }
                }
                
                // For the indices that fall in between the truncation points, we need to perform the matrix-vector
                // multiplication, scale and add the result.
                ValueType weight = 0;
                std::function<ValueType(ValueType const&, ValueType const&)> addAndScale = [&weight] (ValueType const& a, ValueType const& b) { return a + weight * b; };
                for (uint_fast64_t index = startingIteration; index <= foxGlynnResult.right; ++index) {
                    multiplier->multiply(env, values, addVector, values);
                    
                    weight = foxGlynnResult.weights[index - foxGlynnResult.left];
                    storm::utility::vector::applyPointwise(result, values, result, addAndScale);
                }
                
                return result;
            }
            
            template <typename ValueType>
            storm::storage::SparseMatrix<ValueType> SparseCtmcCslHelper::computeProbabilityMatrix(storm::storage::SparseMatrix<ValueType> const& rateMatrix, std::vector<ValueType> const& exitRates) {
                // Turn the rates into probabilities by scaling each row with the exit rate of the state.
                storm::storage::SparseMatrix<ValueType> result(rateMatrix);
                for (uint_fast64_t row = 0; row < result.getRowCount(); ++row) {
                    for (auto& entry : result.getRow(row)) {
                        entry.setValue(entry.getValue() / exitRates[row]);
                    }
                }
                return result;
            }
            
            template <typename ValueType>
            storm::storage::SparseMatrix<ValueType> SparseCtmcCslHelper::computeGeneratorMatrix(storm::storage::SparseMatrix<ValueType> const& rateMatrix, std::vector<ValueType> const& exitRates) {
                storm::storage::SparseMatrix<ValueType> generatorMatrix(rateMatrix, true);
                
                // Place the negative exit rate on the diagonal.
                for (uint_fast64_t row = 0; row < generatorMatrix.getRowCount(); ++row) {
                    for (auto& entry : generatorMatrix.getRow(row)) {
                        if (entry.getColumn() == row) {
                            entry.setValue(-exitRates[row] + entry.getValue());
                        }
                    }
                }
                
                return generatorMatrix;
            }
            
            
            template std::vector<double> SparseCtmcCslHelper::computeBoundedUntilProbabilities(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& rateMatrix, storm::storage::SparseMatrix<double> const& backwardTransitions, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, std::vector<double> const& exitRates, bool qualitative, double lowerBound, double upperBound);
            
            template std::vector<double> SparseCtmcCslHelper::computeUntilProbabilities(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& rateMatrix, storm::storage::SparseMatrix<double> const& backwardTransitions, std::vector<double> const& exitRateVector, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, bool qualitative);

            template std::vector<double> SparseCtmcCslHelper::computeAllUntilProbabilities(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& rateMatrix, std::vector<double> const& exitRateVector, storm::storage::BitVector const& initialStates, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates);

            template std::vector<double> SparseCtmcCslHelper::computeNextProbabilities(Environment const& env, storm::storage::SparseMatrix<double> const& rateMatrix, std::vector<double> const& exitRateVector, storm::storage::BitVector const& nextStates);
            
            template std::vector<double> SparseCtmcCslHelper::computeInstantaneousRewards(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& rateMatrix, std::vector<double> const& exitRateVector, storm::models::sparse::StandardRewardModel<double> const& rewardModel, double timeBound);
            
            template std::vector<double> SparseCtmcCslHelper::computeReachabilityTimes(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& rateMatrix, storm::storage::SparseMatrix<double> const& backwardTransitions, std::vector<double> const& exitRateVector, storm::storage::BitVector const& targetStates, bool qualitative);
            
            template std::vector<double> SparseCtmcCslHelper::computeReachabilityRewards(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& rateMatrix, storm::storage::SparseMatrix<double> const& backwardTransitions, std::vector<double> const& exitRateVector, storm::models::sparse::StandardRewardModel<double> const& rewardModel, storm::storage::BitVector const& targetStates, bool qualitative);
            
            template std::vector<double> SparseCtmcCslHelper::computeTotalRewards(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& rateMatrix, storm::storage::SparseMatrix<double> const& backwardTransitions, std::vector<double> const& exitRateVector, storm::models::sparse::StandardRewardModel<double> const& rewardModel, bool qualitative);
            
            template std::vector<double> SparseCtmcCslHelper::computeLongRunAverageProbabilities(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& probabilityMatrix, storm::storage::BitVector const& psiStates, std::vector<double> const* exitRateVector);
            template std::vector<double> SparseCtmcCslHelper::computeLongRunAverageRewards(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& probabilityMatrix, storm::models::sparse::StandardRewardModel<double> const& rewardModel, std::vector<double> const* exitRateVector);
            template std::vector<double> SparseCtmcCslHelper::computeLongRunAverageRewards(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& probabilityMatrix, std::vector<double> const& stateRewardVector, std::vector<double> const* exitRateVector);
            
            template std::vector<double> SparseCtmcCslHelper::computeCumulativeRewards(Environment const& env, storm::solver::SolveGoal<double>&& goal, storm::storage::SparseMatrix<double> const& rateMatrix, std::vector<double> const& exitRateVector, storm::models::sparse::StandardRewardModel<double> const& rewardModel, double timeBound);

            template std::vector<double> SparseCtmcCslHelper::computeAllTransientProbabilities(Environment const& env, storm::storage::SparseMatrix<double> const& rateMatrix, storm::storage::BitVector const& initialStates, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, std::vector<double> const& exitRates, double timeBound);
            
            template storm::storage::SparseMatrix<double> SparseCtmcCslHelper::computeUniformizedMatrix(storm::storage::SparseMatrix<double> const& rateMatrix, storm::storage::BitVector const& maybeStates, double uniformizationRate, std::vector<double> const& exitRates);
            
            template std::vector<double> SparseCtmcCslHelper::computeTransientProbabilities(Environment const& env, storm::storage::SparseMatrix<double> const& uniformizedMatrix, std::vector<double> const* addVector, double timeBound, double uniformizationRate, std::vector<double> values);

#ifdef STORM_HAVE_CARL
            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeBoundedUntilProbabilities(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, storm::storage::SparseMatrix<storm::RationalNumber> const& backwardTransitions, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, std::vector<storm::RationalNumber> const& exitRates, bool qualitative, double lowerBound, double upperBound);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeBoundedUntilProbabilities(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, storm::storage::SparseMatrix<storm::RationalFunction> const& backwardTransitions, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, std::vector<storm::RationalFunction> const& exitRates, bool qualitative, double lowerBound, double upperBound);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeUntilProbabilities(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, storm::storage::SparseMatrix<storm::RationalNumber> const& backwardTransitions, std::vector<storm::RationalNumber> const& exitRateVector, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, bool qualitative);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeUntilProbabilities(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, storm::storage::SparseMatrix<storm::RationalFunction> const& backwardTransitions, std::vector<storm::RationalFunction> const& exitRateVector, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, bool qualitative);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeAllUntilProbabilities(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, std::vector<storm::RationalNumber> const& exitRateVector, storm::storage::BitVector const& initialStates, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeAllUntilProbabilities(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, std::vector<storm::RationalFunction> const& exitRateVector, storm::storage::BitVector const& initialStates, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeNextProbabilities(Environment const& env, storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, std::vector<storm::RationalNumber> const& exitRateVector, storm::storage::BitVector const& nextStates);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeNextProbabilities(Environment const& env, storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, std::vector<storm::RationalFunction> const& exitRateVector, storm::storage::BitVector const& nextStates);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeInstantaneousRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, std::vector<storm::RationalNumber> const& exitRateVector, storm::models::sparse::StandardRewardModel<storm::RationalNumber> const& rewardModel, double timeBound);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeInstantaneousRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, std::vector<storm::RationalFunction> const& exitRateVector, storm::models::sparse::StandardRewardModel<storm::RationalFunction> const& rewardModel, double timeBound);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeReachabilityTimes(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, storm::storage::SparseMatrix<storm::RationalNumber> const& backwardTransitions, std::vector<storm::RationalNumber> const& exitRateVector, storm::storage::BitVector const& targetStates, bool qualitative);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeReachabilityTimes(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, storm::storage::SparseMatrix<storm::RationalFunction> const& backwardTransitions, std::vector<storm::RationalFunction> const& exitRateVector, storm::storage::BitVector const& targetStates, bool qualitative);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeReachabilityRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, storm::storage::SparseMatrix<storm::RationalNumber> const& backwardTransitions, std::vector<storm::RationalNumber> const& exitRateVector, storm::models::sparse::StandardRewardModel<storm::RationalNumber> const& rewardModel, storm::storage::BitVector const& targetStates, bool qualitative);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeReachabilityRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, storm::storage::SparseMatrix<storm::RationalFunction> const& backwardTransitions, std::vector<storm::RationalFunction> const& exitRateVector, storm::models::sparse::StandardRewardModel<storm::RationalFunction> const& rewardModel, storm::storage::BitVector const& targetStates, bool qualitative);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeTotalRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, storm::storage::SparseMatrix<storm::RationalNumber> const& backwardTransitions, std::vector<storm::RationalNumber> const& exitRateVector, storm::models::sparse::StandardRewardModel<storm::RationalNumber> const& rewardModel, bool qualitative);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeTotalRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, storm::storage::SparseMatrix<storm::RationalFunction> const& backwardTransitions, std::vector<storm::RationalFunction> const& exitRateVector, storm::models::sparse::StandardRewardModel<storm::RationalFunction> const& rewardModel, bool qualitative);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeLongRunAverageProbabilities(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& probabilityMatrix, storm::storage::BitVector const& psiStates, std::vector<storm::RationalNumber> const* exitRateVector);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeLongRunAverageProbabilities(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& probabilityMatrix, storm::storage::BitVector const& psiStates, std::vector<storm::RationalFunction> const* exitRateVector);
            
            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeLongRunAverageRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& probabilityMatrix, storm::models::sparse::StandardRewardModel<RationalNumber> const& rewardModel, std::vector<storm::RationalNumber> const* exitRateVector);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeLongRunAverageRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& probabilityMatrix, storm::models::sparse::StandardRewardModel<RationalFunction> const& rewardModel, std::vector<storm::RationalFunction> const* exitRateVector);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeLongRunAverageRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& probabilityMatrix, std::vector<storm::RationalNumber> const& stateRewardVector, std::vector<storm::RationalNumber> const* exitRateVector);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeLongRunAverageRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& probabilityMatrix, std::vector<storm::RationalFunction> const& stateRewardVector, std::vector<storm::RationalFunction> const* exitRateVector);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeCumulativeRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalNumber>&& goal, storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, std::vector<storm::RationalNumber> const& exitRateVector, storm::models::sparse::StandardRewardModel<storm::RationalNumber> const& rewardModel, double timeBound);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeCumulativeRewards(Environment const& env, storm::solver::SolveGoal<storm::RationalFunction>&& goal, storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, std::vector<storm::RationalFunction> const& exitRateVector, storm::models::sparse::StandardRewardModel<storm::RationalFunction> const& rewardModel, double timeBound);

            template std::vector<storm::RationalNumber> SparseCtmcCslHelper::computeAllTransientProbabilities(Environment const& env, storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, storm::storage::BitVector const& initialStates, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, std::vector<storm::RationalNumber> const& exitRates, double timeBound);
            template std::vector<storm::RationalFunction> SparseCtmcCslHelper::computeAllTransientProbabilities(Environment const& env, storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, storm::storage::BitVector const& initialStates, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, std::vector<storm::RationalFunction> const& exitRates, double timeBound);

            template storm::storage::SparseMatrix<double> SparseCtmcCslHelper::computeProbabilityMatrix(storm::storage::SparseMatrix<double> const& rateMatrix, std::vector<double> const& exitRates);
            template storm::storage::SparseMatrix<storm::RationalNumber> SparseCtmcCslHelper::computeProbabilityMatrix(storm::storage::SparseMatrix<storm::RationalNumber> const& rateMatrix, std::vector<storm::RationalNumber> const& exitRates);
            template storm::storage::SparseMatrix<storm::RationalFunction> SparseCtmcCslHelper::computeProbabilityMatrix(storm::storage::SparseMatrix<storm::RationalFunction> const& rateMatrix, std::vector<storm::RationalFunction> const& exitRates);

#endif
        }
    }
}
