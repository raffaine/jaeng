add_library(renderer_api INTERFACE)
target_include_directories(renderer_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/render/public)

set(JAENG_SOURCES
  common/pubsub.h
  common/logging.h
  common/result.h
  common/app_state.h
  common/app_state.cpp
  common/default_assets.h
  common/default_assets.cpp
  common/async/task_scheduler.h
  common/async/task_scheduler.cpp
  common/async/task.h
  common/async/awaiters.h
  common/async/awaiters.cpp
  platform/public/platform_api.h
  platform/public/application.cpp
  render/frontend/renderer.h
  render/graph/render_graph.h
  storage/ifstorage.h
  material/imaterialsys.h
  material/materialsys.cpp
  mesh/imeshsys.h
  mesh/meshsys.cpp
  texture/itexturesys.h
  texture/texturesys.cpp
  animation/animation.cpp
  entity/entity.h
  scene/scene.cpp
  scene/render_sys.cpp
  scene/grid_partition.cpp
  scene/perspective_cam.cpp
  ui/ui.cpp
  ui/fontsys.cpp
)

if(WIN32)
  include(platform/win32/platform.cmake)
elseif(APPLE)
  include(platform/apple/apple.cmake)
elseif(ANDROID)
  include(platform/android/platform.cmake)
elseif(UNIX AND NOT APPLE AND NOT ANDROID)
  include(platform/wayland/platform.cmake)
endif()

add_library(jaeng STATIC ${JAENG_SOURCES})
add_library(jaeng::engine ALIAS jaeng)

target_compile_definitions(jaeng PUBLIC ${JAENG_PLATFORM_DEF})
target_compile_features(jaeng PUBLIC cxx_std_23)

if(WIN32)
  jaeng_configure_platform_win32(jaeng)
elseif(APPLE)
  jaeng_configure_platform_apple(jaeng)
elseif(ANDROID)
  jaeng_configure_platform_android(jaeng)
elseif(UNIX AND NOT APPLE AND NOT ANDROID)
  jaeng_configure_platform_wayland(jaeng)
endif()

target_include_directories(jaeng PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${Stb_INCLUDE_DIR} ${CGLTF_INCLUDE_DIRS})
target_link_libraries(jaeng PUBLIC renderer_api glm::glm)
target_link_libraries(jaeng PUBLIC nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator)
target_link_libraries(jaeng PUBLIC tinyobjloader::tinyobjloader)
