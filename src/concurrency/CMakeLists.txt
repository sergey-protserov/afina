set(SOURCE_FILES
  Executor.cpp
)

add_library(Concurrency ${SOURCE_FILES})
target_link_libraries(Concurrency ${CMAKE_THREAD_LIBS_INIT})
