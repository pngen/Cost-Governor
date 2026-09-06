add_executable(costgovernor_tests
  test_main.cpp
  test_money.cpp
  test_budget.cpp
  test_plan.cpp
  test_authority.cpp
  test_retry.cpp
  test_persistence.cpp
  test_property.cpp
  test_adversarial.cpp
  test_concurrency.cpp
)
target_link_libraries(costgovernor_tests PRIVATE CostGovernor::CostGovernor Threads::Threads)
if(MSVC)
  target_compile_options(costgovernor_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(costgovernor_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()
add_test(NAME costgovernor_tests COMMAND costgovernor_tests)
