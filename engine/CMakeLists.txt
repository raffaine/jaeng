add_library(renderer_api INTERFACE)
target_include_directories(renderer_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/render/public)

add_library(jaeng STATIC
  common/pubsub.h
  platform/public/platform_api.h
  platform/win32/win32_platform.h
  platform/win32/win32_platform.cpp
  render/frontend/renderer.h
  render/graph/render_graph.h
  storage/ifstorage.h
  storage/win/filestorage.cpp
  material/imaterialsys.h
  material/materialsys.cpp
  mesh/imeshsys.h
  mesh/meshsys.cpp
  entity/entity.h
  scene/scene.cpp
  scene/grid_partition.cpp
  scene/perspective_cam.cpp
)

target_compile_definitions(jaeng PRIVATE UNICODE= _UNICODE=)
target_include_directories(jaeng PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jaeng PUBLIC renderer_api glm::glm-header-only)
target_link_libraries(jaeng PUBLIC nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator)
