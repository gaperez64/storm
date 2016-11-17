#include "src/storm/solver/stateelimination/MultiValueStateEliminator.h"

#include "src/storm/utility/constants.h"

namespace storm {
    namespace solver {
        namespace stateelimination {
            
            template<typename ValueType>
            MultiValueStateEliminator<ValueType>::MultiValueStateEliminator(storm::storage::FlexibleSparseMatrix<ValueType>& transitionMatrix, storm::storage::FlexibleSparseMatrix<ValueType>& backwardTransitions, PriorityQueuePointer priorityQueue, std::vector<ValueType>& stateValues, std::vector<ValueType>& additionalStateValuesVector) : PrioritizedStateEliminator<ValueType>(transitionMatrix, backwardTransitions, priorityQueue, stateValues), additionalStateValues({std::ref(additionalStateValuesVector)}) {

            }

            template<typename ValueType>
            MultiValueStateEliminator<ValueType>::MultiValueStateEliminator(storm::storage::FlexibleSparseMatrix<ValueType>& transitionMatrix, storm::storage::FlexibleSparseMatrix<ValueType>& backwardTransitions, std::vector<storm::storage::sparse::state_type> const& statesToEliminate, std::vector<ValueType>& stateValues, std::vector<ValueType>& additionalStateValuesVector) : PrioritizedStateEliminator<ValueType>(transitionMatrix, backwardTransitions, statesToEliminate, stateValues), additionalStateValues({std::ref(additionalStateValuesVector)}) {

            }

            template<typename ValueType>
            void MultiValueStateEliminator<ValueType>::updateValue(storm::storage::sparse::state_type const& state, ValueType const& loopProbability) {
                this->stateValues[state] = storm::utility::simplify(loopProbability * this->stateValues[state]);
                for(auto additionalStateValueVectorRef : additionalStateValues) {
                    additionalStateValueVectorRef.get()[state] = storm::utility::simplify(loopProbability * additionalStateValueVectorRef.get()[state]);
                }
            }
       
            template<typename ValueType>
            void MultiValueStateEliminator<ValueType>::updatePredecessor(storm::storage::sparse::state_type const& predecessor, ValueType const& probability, storm::storage::sparse::state_type const& state) {
                this->stateValues[predecessor] = storm::utility::simplify(this->stateValues[predecessor] + storm::utility::simplify(probability * this->stateValues[state]));
                for(auto additionalStateValueVectorRef : additionalStateValues) {
                    additionalStateValueVectorRef.get()[predecessor] = storm::utility::simplify(additionalStateValueVectorRef.get()[predecessor] + storm::utility::simplify(probability * additionalStateValueVectorRef.get()[state]));
                }
            }

            template<typename ValueType>
            void MultiValueStateEliminator<ValueType>::clearStateValues(storm::storage::sparse::state_type const& state) {
                super::clearStateValues(state);
                for(auto additionStateValueVectorRef : additionalStateValues) {
                    additionStateValueVectorRef.get()[state] = storm::utility::zero<ValueType>();
                }
            }
            
            template class MultiValueStateEliminator<double>;

#ifdef STORM_HAVE_CARL
            template class MultiValueStateEliminator<storm::RationalNumber>;
            template class MultiValueStateEliminator<storm::RationalFunction>;
#endif
        } // namespace stateelimination
    } // namespace storage
} // namespace storm