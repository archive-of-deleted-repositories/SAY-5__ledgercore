set(LEDGERCORE_UNIT_SOURCES
    unit/test_transaction.cpp
    unit/test_account.cpp
    unit/test_ledger.cpp
    unit/test_wal_format.cpp
    unit/test_wal_writer.cpp
    unit/test_wal_replay.cpp
    unit/test_idem_cache.cpp
)

foreach(extra IN ITEMS
        unit/test_spsc.cpp
        unit/test_dispatcher.cpp
        unit/test_worker.cpp)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${extra}")
        list(APPEND LEDGERCORE_UNIT_SOURCES ${extra})
    endif()
endforeach()

add_executable(unit_tests ${LEDGERCORE_UNIT_SOURCES})
target_link_libraries(unit_tests PRIVATE ledgercore_core GTest::gtest GTest::gtest_main GTest::gmock)
include(GoogleTest)
gtest_discover_tests(unit_tests PROPERTIES LABELS "unit")

set(LEDGERCORE_CONCURRENCY_SOURCES "")
foreach(extra IN ITEMS
        unit/test_spsc_concurrency.cpp
        unit/test_dispatcher_concurrency.cpp)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${extra}")
        list(APPEND LEDGERCORE_CONCURRENCY_SOURCES ${extra})
    endif()
endforeach()

if(LEDGERCORE_CONCURRENCY_SOURCES)
    add_executable(concurrency_tests ${LEDGERCORE_CONCURRENCY_SOURCES})
    target_link_libraries(concurrency_tests PRIVATE ledgercore_core GTest::gtest GTest::gtest_main GTest::gmock)
    gtest_discover_tests(concurrency_tests PROPERTIES LABELS "concurrency")
endif()

if(LEDGERCORE_ENABLE_POSTGRES)
    set(LEDGERCORE_IT_SOURCES "")
    foreach(extra IN ITEMS
            integration/test_pg_idem.cpp
            integration/test_end_to_end.cpp)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${extra}")
            list(APPEND LEDGERCORE_IT_SOURCES ${extra})
        endif()
    endforeach()
    if(LEDGERCORE_IT_SOURCES)
        add_executable(integration_tests ${LEDGERCORE_IT_SOURCES})
        target_link_libraries(integration_tests PRIVATE ledgercore_core GTest::gtest GTest::gtest_main)
        gtest_discover_tests(integration_tests PROPERTIES LABELS "integration")
    endif()
endif()
