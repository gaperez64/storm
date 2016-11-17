#include "src/storm/settings/SettingMemento.h"

#include "src/storm/settings/modules/ModuleSettings.h"

namespace storm {
    namespace settings {
        SettingMemento::SettingMemento(modules::ModuleSettings& settings, std::string const& longOptionName, bool resetToState) : settings(settings), optionName(longOptionName), resetToState(resetToState) {
            // Intentionally left empty.
        }
        
        /*!
         * Destructs the memento object and resets the value of the option to its original state.
         */
        SettingMemento::~SettingMemento() {
            if (resetToState) {
                settings.set(optionName);
            } else {
                settings.unset(optionName);
            }
        }
    }
}