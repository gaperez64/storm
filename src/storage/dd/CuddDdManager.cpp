#include <cmath>
#include <string>
#include <algorithm>

#include "src/storage/dd/CuddDdManager.h"
#include "src/exceptions/InvalidArgumentException.h"

namespace storm {
    namespace dd {
        DdManager<DdType::CUDD>::DdManager() : metaVariableMap(), cuddManager() {
            
            this->cuddManager.SetEpsilon(1.0e-15);
        }
        
        Dd<DdType::CUDD> DdManager<DdType::CUDD>::getOne() {
            return Dd<DdType::CUDD>(this->shared_from_this(), cuddManager.addOne());
        }
        
        Dd<DdType::CUDD> DdManager<DdType::CUDD>::getZero() {
            return Dd<DdType::CUDD>(this->shared_from_this(), cuddManager.addZero());
        }
        
        Dd<DdType::CUDD> DdManager<DdType::CUDD>::getConstant(double value) {
            return Dd<DdType::CUDD>(this->shared_from_this(), cuddManager.constant(value));
        }
        
        Dd<DdType::CUDD> DdManager<DdType::CUDD>::getEncoding(std::string const& metaVariableName, int_fast64_t value) {
            // Check whether the meta variable exists.
            if (!this->hasMetaVariable(metaVariableName)) {
                throw storm::exceptions::InvalidArgumentException() << "Unknown meta variable name '" << metaVariableName << "'.";
            }
            
            DdMetaVariable<DdType::CUDD> const& metaVariable = this->getMetaVariable(metaVariableName);
            
            // Check whether the value is legal for this meta variable.
            if (value < metaVariable.getLow() || value > metaVariable.getHigh()) {
                throw storm::exceptions::InvalidArgumentException() << "Illegal value " << value << " for meta variable '" << metaVariableName << "'.";
            }
            
            // Now compute the encoding relative to the low value of the meta variable.
            value -= metaVariable.getLow();
            
            std::vector<Dd<DdType::CUDD>> const& ddVariables = metaVariable.getDdVariables();

            Dd<DdType::CUDD> result;
            if (value & (1ull << (ddVariables.size() - 1))) {
                result = ddVariables[0];
            } else {
                result = !ddVariables[0];
            }
            
            for (std::size_t i = 1; i < ddVariables.size(); ++i) {
                if (value & (1ull << (ddVariables.size() - i - 1))) {
                    result *= ddVariables[i];
                } else {
                    result *= !ddVariables[i];
                }
            }
                        
            return result;
        }
        
        Dd<DdType::CUDD> DdManager<DdType::CUDD>::getRange(std::string const& metaVariableName) {
            // Check whether the meta variable exists.
            if (!this->hasMetaVariable(metaVariableName)) {
                throw storm::exceptions::InvalidArgumentException() << "Unknown meta variable name '" << metaVariableName << "'.";
            }

            storm::dd::DdMetaVariable<DdType::CUDD> const& metaVariable = this->getMetaVariable(metaVariableName);
            
            Dd<DdType::CUDD> result = this->getZero();
            
            for (int_fast64_t value = metaVariable.getLow(); value <= metaVariable.getHigh(); ++value) {
                result.setValue(metaVariableName, value, static_cast<double>(1));
            }
            return result;
        }
        
        Dd<DdType::CUDD> DdManager<DdType::CUDD>::getIdentity(std::string const& metaVariableName) {
            // Check whether the meta variable exists.
            if (!this->hasMetaVariable(metaVariableName)) {
                throw storm::exceptions::InvalidArgumentException() << "Unknown meta variable name '" << metaVariableName << "'.";
            }
            
            storm::dd::DdMetaVariable<DdType::CUDD> const& metaVariable = this->getMetaVariable(metaVariableName);
            
            Dd<DdType::CUDD> result = this->getZero();
            for (int_fast64_t value = metaVariable.getLow(); value <= metaVariable.getHigh(); ++value) {
                result.setValue(metaVariableName, value, static_cast<double>(value));
            }
            return result;
        }

        void DdManager<DdType::CUDD>::addMetaVariable(std::string const& name, int_fast64_t low, int_fast64_t high) {
            // Check whether a meta variable already exists.
            if (this->hasMetaVariable(name)) {
                throw storm::exceptions::InvalidArgumentException() << "A meta variable '" << name << "' already exists.";
            }

            // Check that the range is legal.
            if (high == low) {
                throw storm::exceptions::InvalidArgumentException() << "Range of meta variable must be at least 2 elements.";
            }
            
            std::size_t numberOfBits = static_cast<std::size_t>(std::ceil(std::log2(high - low + 1)));
            
            std::vector<Dd<DdType::CUDD>> variables;
            for (std::size_t i = 0; i < numberOfBits; ++i) {
                variables.emplace_back(Dd<DdType::CUDD>(this->shared_from_this(), cuddManager.addVar(), {name}));
            }
            
            metaVariableMap.emplace(name, DdMetaVariable<DdType::CUDD>(name, low, high, variables, this->shared_from_this()));
        }
        
        void DdManager<DdType::CUDD>::addMetaVariable(std::string const& name) {
            // Check whether a meta variable already exists.
            if (this->hasMetaVariable(name)) {
                throw storm::exceptions::InvalidArgumentException() << "A meta variable '" << name << "' already exists.";
            }
            
            std::vector<Dd<DdType::CUDD>> variables;
            variables.emplace_back(Dd<DdType::CUDD>(this->shared_from_this(), cuddManager.addVar(), {name}));
            
            metaVariableMap.emplace(name, DdMetaVariable<DdType::CUDD>(name, variables, this->shared_from_this()));
        }
        
        void DdManager<DdType::CUDD>::addMetaVariablesInterleaved(std::vector<std::string> const& names, int_fast64_t low, int_fast64_t high) {
            // Make sure that at least one meta variable is added.
            if (names.size() == 0) {
                throw storm::exceptions::InvalidArgumentException() << "Illegal to add zero meta variables.";
            }
            
            // Check that there are no duplicate names in the given name vector.
            std::vector<std::string> nameCopy(names);
            std::sort(nameCopy.begin(), nameCopy.end());
            if (std::adjacent_find(nameCopy.begin(), nameCopy.end()) != nameCopy.end()) {
                throw storm::exceptions::InvalidArgumentException() << "Cannot add duplicate meta variables.";
            }
            
            // Check that the range is legal.
            if (high == low) {
                throw storm::exceptions::InvalidArgumentException() << "Range of meta variable must be at least 2 elements.";
            }

            // Check whether a meta variable already exists.
            for (auto const& metaVariableName : names) {
                if (this->hasMetaVariable(metaVariableName)) {
                    throw storm::exceptions::InvalidArgumentException() << "A meta variable '" << metaVariableName << "' already exists.";
                }
            }
            
            // Add the variables in interleaved order.
            std::size_t numberOfBits = static_cast<std::size_t>(std::ceil(std::log2(high - low + 1)));
            std::vector<std::vector<Dd<DdType::CUDD>>> variables(names.size());
            for (uint_fast64_t bit = 0; bit < numberOfBits; ++bit) {
                for (uint_fast64_t i = 0; i < names.size(); ++i) {
                    variables[i].emplace_back(Dd<DdType::CUDD>(this->shared_from_this(), cuddManager.addVar(), {names[i]}));
                }
            }
            
            // Now add the meta variables.
            for (uint_fast64_t i = 0; i < names.size(); ++i) {
                metaVariableMap.emplace(names[i], DdMetaVariable<DdType::CUDD>(names[i], low, high, variables[i], this->shared_from_this()));
            }
        }
        
        DdMetaVariable<DdType::CUDD> const& DdManager<DdType::CUDD>::getMetaVariable(std::string const& metaVariableName) const {
            auto const& nameVariablePair = metaVariableMap.find(metaVariableName);
            
            if (!this->hasMetaVariable(metaVariableName)) {
                throw storm::exceptions::InvalidArgumentException() << "Unknown meta variable name '" << metaVariableName << "'.";
            }
            
            return nameVariablePair->second;
        }
        
        std::set<std::string> DdManager<DdType::CUDD>::getAllMetaVariableNames() const {
            std::set<std::string> result;
            for (auto const& nameValuePair : metaVariableMap) {
                result.insert(nameValuePair.first);
            }
            return result;
        }
        
        std::size_t DdManager<DdType::CUDD>::getNumberOfMetaVariables() const {
            return this->metaVariableMap.size();
        }
        
        bool DdManager<DdType::CUDD>::hasMetaVariable(std::string const& metaVariableName) const {
            return this->metaVariableMap.find(metaVariableName) != this->metaVariableMap.end();
        }
        
        Cudd& DdManager<DdType::CUDD>::getCuddManager() {
            return this->cuddManager;
        }
        
        std::vector<std::string> DdManager<DdType::CUDD>::getDdVariableNames() const {
            // First, we initialize a list DD variables and their names.
            std::vector<std::pair<ADD, std::string>> variableNamePairs;
            for (auto const& nameMetaVariablePair : this->metaVariableMap) {
                DdMetaVariable<DdType::CUDD> const& metaVariable = nameMetaVariablePair.second;
                for (uint_fast64_t variableIndex = 0; variableIndex < metaVariable.getNumberOfDdVariables(); ++variableIndex) {
                    variableNamePairs.emplace_back(metaVariable.getDdVariables()[variableIndex].getCuddAdd(), metaVariable.getName() + "." + std::to_string(variableIndex));
                }
            }
            
            // Then, we sort this list according to the indices of the ADDs.
            std::sort(variableNamePairs.begin(), variableNamePairs.end(), [](std::pair<ADD, std::string> const& a, std::pair<ADD, std::string> const& b) { return a.first.NodeReadIndex() < b.first.NodeReadIndex(); });
            
            // Now, we project the sorted vector to its second component.
            std::vector<std::string> result;
            for (auto const& element : variableNamePairs) {
                result.push_back(element.second);
            }
            
            return result;
        }
    }
}