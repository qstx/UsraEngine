ursa_add_test_executable(UrsaProfilerCompileTests ProfilerCompileTests.cpp)
target_link_libraries(UrsaProfilerCompileTests PRIVATE Ursa::Runtime)

add_test(NAME UrsaProfilerCompileTests COMMAND UrsaProfilerCompileTests)

ursa_add_test_executable(UrsaProfilerMemoryTests ProfilerMemoryTests.cpp)
target_link_libraries(UrsaProfilerMemoryTests PRIVATE Ursa::Runtime)

add_test(NAME UrsaProfilerMemoryTests COMMAND UrsaProfilerMemoryTests)

add_test(NAME UrsaProfilerGpuMemoryTests COMMAND UrsaProfilerMemoryTests --gpu)

ursa_add_test_executable(UrsaEastlAllocatorTests EastlAllocatorTests.cpp)
target_link_libraries(UrsaEastlAllocatorTests PRIVATE Ursa::Runtime)

add_test(NAME UrsaEastlAllocatorTests COMMAND UrsaEastlAllocatorTests)

ursa_add_test_executable(UrsaMemoryAllocatorTests MemoryAllocatorTests.cpp)
target_link_libraries(UrsaMemoryAllocatorTests PRIVATE Ursa::Runtime)

add_test(NAME UrsaMemoryAllocatorTests COMMAND UrsaMemoryAllocatorTests)

ursa_add_test_executable(UrsaTemporaryAllocatorTests TemporaryAllocatorTests.cpp)
target_link_libraries(UrsaTemporaryAllocatorTests PRIVATE Ursa::Runtime)

add_test(NAME UrsaTemporaryAllocatorTests COMMAND UrsaTemporaryAllocatorTests)

ursa_add_test_executable(UrsaMemoryFactoryTests MemoryFactoryTests.cpp)
target_link_libraries(UrsaMemoryFactoryTests PRIVATE Ursa::Runtime)

add_test(NAME UrsaMemoryFactoryTests COMMAND UrsaMemoryFactoryTests)

ursa_add_test_executable(UrsaLogTests LogTests.cpp)
target_link_libraries(UrsaLogTests PRIVATE Ursa::Runtime)

ursa_add_output_test(UrsaLogTests UrsaLogTests "\\[RHI\\].*")

ursa_add_test_executable(UrsaStringInteropTests StringInteropTests.cpp)
target_link_libraries(UrsaStringInteropTests PRIVATE Ursa::Runtime)

add_test(NAME UrsaStringInteropTests COMMAND UrsaStringInteropTests)

ursa_finalize_test_directory()
