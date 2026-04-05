add_library(renderer_api INTERFACE)
target_include_directories(renderer_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/render/public)

set(JAENG_SOURCES
  common/pubsub.h
  common/logging.h
  common/result.h
  platform/public/platform_api.h
  platform/public/application.cpp
  render/frontend/renderer.h
  render/graph/render_graph.h
  storage/ifstorage.h
  material/imaterialsys.h
  material/materialsys.cpp
  mesh/imeshsys.h
  mesh/meshsys.cpp
  entity/entity.h
  scene/scene.cpp
  scene/grid_partition.cpp
  scene/perspective_cam.cpp
)

if(WIN32)
  include(platform/win32/platform.cmake)
elseif(UNIX AND NOT APPLE)
  include(platform/wayland/platform.cmake)
endif()

add_library(jaeng STATIC ${JAENG_SOURCES})
target_compile_definitions(jaeng PUBLIC ${JAENG_PLATFORM_DEF})

if(WIN32)
  jaeng_configure_platform_win32(jaeng)
elseif(UNIX AND NOT APPLE)
  jaeng_configure_platform_wayland(jaeng)
endif()

target_include_directories(jaeng PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jaeng PUBLIC renderer_api glm::glm)
target_link_libraries(jaeng PUBLIC nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator)
