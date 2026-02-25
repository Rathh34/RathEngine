#pragma once
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <any>

namespace Rath {

    class EventBus {
    public:
        template <typename TEvent>
        using Handler = std::function<void(const TEvent&)>;

        template <typename TEvent>
        void Subscribe(Handler<TEvent> handler) {
            m_Handlers[std::type_index(typeid(TEvent))].emplace_back(
                [h = std::move(handler)](const std::any& e) {
                    h(std::any_cast<const TEvent&>(e));
                }
            );
        }

        template <typename TEvent>
        void Emit(const TEvent& event) {
            std::unordered_map<
                std::type_index,
                std::vector<std::function<void(const std::any&)>>
            >::iterator it = m_Handlers.find(std::type_index(typeid(TEvent)));

            if (it == m_Handlers.end()) return;

            std::any wrapped = event;

            std::vector<std::function<void(const std::any&)>>& vec = it->second;
            for (std::function<void(const std::any&)>& fn : vec) {
                fn(wrapped);
            }
        }

    private:
        std::unordered_map<
            std::type_index,
            std::vector<std::function<void(const std::any&)>>
        > m_Handlers;
    };

} // namespace Rath
