#pragma once

#include "DFTChildren.h"

namespace storm {
    namespace storage {

        /*!
         * Abstract base class for restrictions.
         * Restrictions prevent the failure of DFT events.
         */
        template<typename ValueType>
        class DFTRestriction : public DFTChildren<ValueType> {
            using DFTElementPointer = std::shared_ptr<DFTElement<ValueType>>;
            using DFTElementVector = std::vector<DFTElementPointer>;

        public:
            /*!
             * Constructor.
             * @param id Id.
             * @param name Name.
             * @param children Children.
             */
            DFTRestriction(size_t id, std::string const& name, DFTElementVector const& children) : DFTChildren<ValueType>(id, name, children) {
                // Intentionally left empty.
            }

            /*!
             * Destructor
             */
            virtual ~DFTRestriction() {
                // Intentionally left empty.
            };

            bool isRestriction() const override {
                return true;
            }

            /*!
             * Return whether the restriction is a sequence enforcer.
             * @return True iff the restriction is a SEQ.
             */
            virtual bool isSeqEnforcer() const {
                return false;
            }

            /*!
             * Returns whether all children are BEs.
             * @return True iff all children are BEs.
             */
            bool allChildrenBEs() const {
                for (auto const& elem : this->children()) {
                    if (!elem->isBasicElement()) {
                        return false;
                    }
                }
                return true;
            }

            void extendSpareModule(std::set<size_t>& elementsInSpareModule) const override {
                // Do nothing
            }

            bool checkDontCareAnymore(storm::storage::DFTState<ValueType>& state, DFTStateSpaceGenerationQueues<ValueType>& queues) const override {
                return false;
            }


        protected:
            void fail(DFTState <ValueType>& state, DFTStateSpaceGenerationQueues<ValueType>& queues) const override {
                state.markAsInvalid();
            }

            void failsafe(DFTState <ValueType>& state, DFTStateSpaceGenerationQueues<ValueType>& queues) const override {
                // Do nothing
            }
        };

    }
}
