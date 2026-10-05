#include <mirrorfly/office_ai.hpp>

#include <type_traits>

int run_office_ai_header_tests()
{
    using namespace mirrorfly;
    static_assert(office_ai_contract_version == 1);
    static_assert(std::is_same_v<decltype(office_ai_contract()), std::string>);
    static_assert(std::is_same_v<decltype(office_execute(std::string{})), std::string>);
    static_assert(std::is_same_v<decltype(office_snapshot()), std::string>);
    static_assert(std::is_same_v<decltype(office_runtime_snapshot()), std::string>);
    static_assert(
        std::is_same_v<decltype(office_subscribe_changes(OfficeChangeObserver{})), OfficeChangeSubscription>);
    static_assert(std::is_same_v<decltype(office_unsubscribe_changes(0)), bool>);
    return 0;
}

int main(int, char**)
{
    return run_office_ai_header_tests();
}
