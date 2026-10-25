cmake_minimum_required(VERSION 3.20)
project(DofusEnhanced LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
# Sources include each other as "<folder>/<file>" from src/, and third-party
# headers as <nlohmann/json.hpp>.
include_directories(src third_party)
add_subdirectory(third_party/minhook EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Shipped binaries. Their file names are part of the release format (loader,
# updater and archive allowlist): never rename these targets.
# ---------------------------------------------------------------------------

# The in-game module. The source order is the link order of every previous
# release; keep it and append new files at the end.
add_library(DofusMiniatures SHARED
  src/core/module.cpp src/core/runtime.cpp src/appearance/features.cpp
  src/animations/animations.cpp src/animations/movement.cpp src/animations/movement-dispatch.cpp
  src/animations/movement-waits.cpp src/animations/push-movement.cpp src/animations/throw-movement.cpp
  src/animations/jump-movement.cpp src/animations/instant-waits.cpp src/animations/script-waits.cpp
  src/animations/combat-cleanup.cpp src/ui/options-ui.cpp src/core/settings.cpp src/core/common.cpp
  src/loader/autoload.cpp src/appearance/size-measurements.cpp src/appearance/display-shortcut.cpp
  src/appearance/context-display.cpp
  src/treasure/treasure-launch.cpp src/treasure/treasure-data.cpp src/treasure/treasure-http.cpp
  src/treasure/treasure-hunt.cpp
  src/ui/tools-ui.cpp src/notebook/notebook-data.cpp src/notebook/notebook-panel.cpp
  src/notebook/notebook-native.cpp src/ui/native-click.cpp
  src/jobs/jobs-xp-data.cpp src/jobs/jobs-xp-http.cpp src/jobs/jobs-xp-panel.cpp
  src/appearance/startup-options.cpp
  src/souls/soul-helper.cpp src/souls/soul-combat.cpp src/souls/soul-inventory.cpp src/souls/soul-panel.cpp
  src/souls/soul-widget.cpp src/maps/map-transitions.cpp src/maps/map-preload.cpp
  src/souls/soul-capture.cpp src/souls/soul-spell.cpp src/souls/soul-item-view.cpp src/souls/soul-layout.cpp
  src/appearance/appearance-refresh.cpp
  src/glow/monster-glow.cpp src/glow/monster-glow-entities.cpp src/glow/monster-glow-ui.cpp
  src/glow/monster-glow-render.cpp src/glow/monster-glow-world.cpp src/glow/monster-glow-interactives.cpp
  src/travel/chat-tp-links.cpp src/travel/chat-travel.cpp src/souls/soul-group-menu.cpp
  src/travel/chat-route.cpp src/travel/map-travel.cpp
  src/social/spouse-party-menu.cpp src/social/spouse-party-widget.cpp
  src/metamob/metamob-data.cpp src/metamob/metamob-secret.cpp src/metamob/metamob-http.cpp
  src/metamob/metamob-sync.cpp src/souls/capture-detect.cpp
  src/maps/archi-zones.cpp src/metamob/calendar-kralove.cpp
  src/souls/chat-soul-status.cpp
  src/maps/resource-map-data.cpp src/maps/resource-map-policy.cpp src/maps/resource-map-ui.cpp
  src/maps/resource-map-render.cpp src/maps/resource-map-icons.cpp
  src/maps/map-info.cpp src/ui/perf-overlay.cpp src/travel/route-graph.cpp src/travel/roublard-passage.cpp
  src/stats/stats.cpp src/stats/stats-travel.cpp src/stats/stats-economy.cpp src/stats/stats-fights.cpp
  src/stats/stats-store.cpp
  src/stats/stats-format.cpp src/stats/stats-window.cpp
  src/ui/cursor-art.cpp src/ui/custom-cursor.cpp
  src/ui/vertical-gauges.cpp)
target_link_libraries(DofusMiniatures PRIVATE bcrypt minhook advapi32 winhttp normaliz user32 crypt32 pdh)

# Loader (console and silent automatic variants) and the runtime probe.
set(LOADER_SOURCES src/loader/launcher.cpp src/core/common.cpp src/loader/autoload.cpp
  src/core/settings.cpp src/loader/update-launch.cpp)
add_executable(ModuleLoader ${LOADER_SOURCES})
target_link_libraries(ModuleLoader PRIVATE bcrypt advapi32 user32)
add_executable(ModuleAutoLoader WIN32 ${LOADER_SOURCES})
target_compile_definitions(ModuleAutoLoader PRIVATE DOFUS_AUTOLOADER)
target_link_libraries(ModuleAutoLoader PRIVATE bcrypt advapi32 user32 shell32)
add_library(RuntimeProbe SHARED src/loader/probe.cpp src/core/common.cpp)
target_link_libraries(RuntimeProbe PRIVATE bcrypt)

# Treasure hunt overlay window (WebView2).
add_executable(TreasureOverlay WIN32 src/treasure/treasure-overlay.cpp)
target_include_directories(TreasureOverlay PRIVATE third_party/webview2/include)
target_link_libraries(TreasureOverlay PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/lib/x64/WebView2LoaderStatic.lib"
  user32 shell32 ole32 uuid version gdi32 advapi32)

if(MSVC)
  target_compile_options(DofusMiniatures PRIVATE /W4 /EHsc /utf-8)
  target_compile_options(TreasureOverlay PRIVATE /W4 /EHsc /utf-8)
  foreach(tool IN ITEMS ModuleLoader ModuleAutoLoader RuntimeProbe)
    target_compile_options(${tool} PRIVATE /W4 /EHsc)
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Offline tests: no game, no network, no personal key.
# ---------------------------------------------------------------------------
enable_testing()

# dofus_test(<name> <sources>... [LIBS <libraries>...]) adds one test program.
function(dofus_test name)
  cmake_parse_arguments(PARSE_ARGV 1 TEST "" "" "LIBS")
  add_executable(${name} ${TEST_UNPARSED_ARGUMENTS})
  target_include_directories(${name} PRIVATE tests)
  if(TEST_LIBS)
    target_link_libraries(${name} PRIVATE ${TEST_LIBS})
  endif()
  if(MSVC)
    target_compile_options(${name} PRIVATE /W4 /EHsc /utf-8)
  endif()
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# Core, loader and updater.
dofus_test(OfflineTests tests/core/offline-tests.cpp src/core/settings.cpp src/core/common.cpp LIBS bcrypt minhook)
dofus_test(RuntimeTests tests/core/runtime-tests.cpp src/core/runtime.cpp src/core/common.cpp LIBS bcrypt)
dofus_test(ReleasePolicyTests tests/core/release-policy-tests.cpp src/core/common.cpp LIBS bcrypt advapi32)
dofus_test(AutoLoadTests tests/loader/autoload-policy-tests.cpp)
add_test(NAME GitHubUpdateTests COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
  -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/update/update-tests.ps1" -BuildDirectory "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(GitHubUpdateTests PROPERTIES TIMEOUT 30)

# Options window and the toolbar module menu.
dofus_test(OptionsUiTests tests/ui/options-ui-tests.cpp src/ui/options-ui.cpp)
dofus_test(ToolsUiTests tests/ui/tools-ui-tests.cpp src/ui/tools-ui.cpp)
# The performance overlay's GPU and disk figures come from PDH (Windows'
# performance counters); the tests replace every PDH call with a fake.
dofus_test(PerfOverlayTests tests/ui/perf-overlay-tests.cpp LIBS pdh)
# 1.17.0 « Curseur Dofus »: the cursors' art, compared with the design's reference renderer.
dofus_test(CursorArtTests tests/ui/cursor-art-tests.cpp src/ui/cursor-art.cpp)
# Its runtime (the SetCursor detour, the textures, the hover inputs) on a fake
# IL2CPP runtime, with fake Windows clock, button, scale and pointer size.
dofus_test(CustomCursorTests tests/ui/custom-cursor-tests.cpp src/ui/cursor-art.cpp LIBS user32 advapi32)
# 1.18.0 « Afficher à la verticale »: the gauges' menu row and their vertical
# layout on a fake IL2CPP runtime (the test includes vertical-gauges.cpp).
dofus_test(VerticalGaugesTests tests/ui/vertical-gauges-tests.cpp)

# Appearance: miniatures, creatures, sizes and display shortcuts.
dofus_test(FeaturesTests tests/appearance/features-tests.cpp src/appearance/features.cpp src/appearance/size-measurements.cpp)
dofus_test(AppearanceRefreshTests tests/appearance/appearance-refresh-tests.cpp src/appearance/appearance-refresh.cpp)
dofus_test(DisplayShortcutTests tests/appearance/display-shortcut-tests.cpp src/appearance/display-shortcut.cpp)
dofus_test(ContextDisplayTests tests/appearance/context-display-tests.cpp src/appearance/context-display.cpp)
dofus_test(StartupOptionsTests tests/appearance/startup-options-tests.cpp src/appearance/startup-options.cpp)

# Animations, movements and visual waits.
dofus_test(AnimationTests tests/animations/animations-tests.cpp)
dofus_test(MovementTests tests/animations/movement-tests.cpp)
dofus_test(MovementDispatchTests tests/animations/movement-dispatch-tests.cpp src/animations/movement-dispatch.cpp)
dofus_test(MovementWaitTests tests/animations/movement-waits-tests.cpp)
dofus_test(InstantWaitTests tests/animations/instant-waits-tests.cpp)
dofus_test(ScriptWaitTests tests/animations/script-waits-tests.cpp)
dofus_test(CombatCleanupTests tests/animations/combat-cleanup-tests.cpp)
foreach(movement IN ITEMS Push Throw Jump)
  string(TOLOWER ${movement} lower)
  dofus_test(${movement}MovementTests tests/animations/${lower}-movement-tests.cpp src/animations/${lower}-movement.cpp)
endforeach()

# Monster glow.
dofus_test(MonsterGlowTests tests/glow/monster-glow-tests.cpp)
foreach(glow IN ITEMS Entities Ui Render World Interactives)
  string(TOLOWER ${glow} lower)
  dofus_test(MonsterGlow${glow}Tests tests/glow/monster-glow-${lower}-tests.cpp src/glow/monster-glow-${lower}.cpp)
endforeach()

# Soul stones, captures and their chat status.
dofus_test(SoulPolicyTests tests/souls/soul-policy-tests.cpp)
dofus_test(SoulCombatTests tests/souls/soul-combat-tests.cpp src/souls/soul-combat.cpp)
dofus_test(SoulInventoryTests tests/souls/soul-inventory-tests.cpp src/souls/soul-inventory.cpp src/souls/soul-helper.cpp)
dofus_test(SoulPanelTests tests/souls/soul-panel-tests.cpp src/souls/soul-panel.cpp)
dofus_test(SoulHelperTests tests/souls/soul-helper-tests.cpp src/souls/soul-helper.cpp)
dofus_test(SoulCaptureTests tests/souls/soul-capture-tests.cpp src/souls/soul-capture.cpp)
dofus_test(SoulWidgetTests tests/souls/soul-widget-tests.cpp src/souls/soul-widget.cpp)
dofus_test(SoulItemViewTests tests/souls/soul-item-view-tests.cpp src/souls/soul-item-view.cpp)
dofus_test(SoulLayoutTests tests/souls/soul-layout-tests.cpp src/souls/soul-layout.cpp)
dofus_test(CaptureDetectTests tests/souls/capture-detect-tests.cpp src/souls/capture-detect.cpp)
dofus_test(ChatSoulStatusTests tests/souls/chat-soul-status-tests.cpp)

# Metamob: data, key, transport, sync and the Kralamoure calendar. Offline only:
# a live test would need a personal key, which is never shipped or committed.
dofus_test(MetamobDataTests tests/metamob/metamob-data-tests.cpp src/metamob/metamob-data.cpp LIBS normaliz)
dofus_test(MetamobSecretTests tests/metamob/metamob-secret-tests.cpp src/metamob/metamob-secret.cpp LIBS crypt32)
dofus_test(MetamobSyncTests tests/metamob/metamob-sync-tests.cpp src/metamob/metamob-sync.cpp
  src/metamob/metamob-data.cpp src/metamob/metamob-secret.cpp LIBS crypt32 normaliz)
# The real transport with every WinHTTP call renamed to a scripted fake;
# deliberately no winhttp: a real WinHTTP call cannot even link.
dofus_test(MetamobHttpTests tests/metamob/metamob-http-tests.cpp src/metamob/metamob-data.cpp
  src/metamob/metamob-secret.cpp LIBS crypt32 normaliz)
dofus_test(CalendarKraloveTests tests/metamob/calendar-kralove-tests.cpp src/metamob/metamob-data.cpp)

# Maps: transitions, preload, resources and the « Ocre » mode.
dofus_test(MapTransitionTests tests/maps/map-transitions-tests.cpp)
dofus_test(MapPreloadTests tests/maps/map-preload-tests.cpp)
dofus_test(ResourceMapPolicyTests tests/maps/resource-map-policy-tests.cpp src/maps/resource-map-policy.cpp
  src/maps/resource-map-data.cpp)
dofus_test(ResourceMapUiTests tests/maps/resource-map-ui-tests.cpp src/maps/resource-map-ui.cpp
  src/maps/resource-map-policy.cpp src/maps/resource-map-icons.cpp)
dofus_test(ResourceMapRenderTests tests/maps/resource-map-render-tests.cpp src/maps/resource-map-render.cpp
  src/maps/resource-map-policy.cpp)
dofus_test(ArchiZonesTests tests/maps/archi-zones-tests.cpp)
dofus_test(MapInfoTests tests/maps/map-info-tests.cpp)

# Travel: chat coordinates, routes, TP links and the map autopilot.
dofus_test(ChatRouteTests tests/travel/chat-route-tests.cpp src/travel/chat-route.cpp src/travel/route-graph.cpp)
dofus_test(RouteGraphTests tests/travel/route-graph-tests.cpp src/travel/route-graph.cpp)
# The route tests run on a synthetic world graph; on the game's own graph too when
# its local fixture exists (derived from a game asset: never committed).
get_filename_component(ROUTE_GRAPH_LOCAL_FIXTURE "${CMAKE_SOURCE_DIR}/../r112-analysis/world-graph-fixture.txt" ABSOLUTE)
if(EXISTS "${ROUTE_GRAPH_LOCAL_FIXTURE}")
  foreach(route IN ITEMS ChatRouteTests RouteGraphTests)
    target_compile_definitions(${route} PRIVATE ROUTE_GRAPH_FIXTURE="${ROUTE_GRAPH_LOCAL_FIXTURE}")
  endforeach()
endif()
dofus_test(RoublardPassageTests tests/travel/roublard-passage-tests.cpp)
# 1.15.0 « Statistiques »: the store and the three trackers.
dofus_test(StatsStoreTests tests/stats/stats-store-tests.cpp src/stats/stats-store.cpp)
dofus_test(StatsTravelTests tests/stats/stats-travel-tests.cpp)
dofus_test(StatsEconomyTests tests/stats/stats-economy-tests.cpp src/stats/stats-economy.cpp)
dofus_test(StatsFightsTests tests/stats/stats-fights-tests.cpp src/stats/stats-fights.cpp)
# 1.16.0: the statistics in their own window, its French texts tested apart.
dofus_test(StatsFormatTests tests/stats/stats-format-tests.cpp src/stats/stats-format.cpp)
dofus_test(StatsWindowTests tests/stats/stats-window-tests.cpp src/stats/stats-window.cpp src/stats/stats-format.cpp
  src/ui/tools-ui.cpp src/ui/native-click.cpp)
dofus_test(ChatTpLinksTests tests/travel/chat-tp-links-tests.cpp src/travel/chat-tp-links.cpp)
add_test(NAME ChatTpColorFallbackTests COMMAND ChatTpLinksTests --missing-color-layout)
add_test(NAME ChatDimensionFallbackTests COMMAND ChatTpLinksTests --missing-dimension-layout)
dofus_test(TravelActionsTests tests/travel/travel-actions-tests.cpp src/travel/chat-travel.cpp
  src/travel/map-travel.cpp src/souls/soul-group-menu.cpp src/souls/soul-spell.cpp src/travel/roublard-passage.cpp)

# Treasure hunts.
dofus_test(TreasureDataTests tests/treasure/treasure-data-tests.cpp src/treasure/treasure-data.cpp
  src/treasure/treasure-http.cpp LIBS winhttp normaliz)
dofus_test(TreasurePanelTests tests/treasure/treasure-panel-tests.cpp src/treasure/treasure-launch.cpp
  src/treasure/treasure-data.cpp src/souls/soul-panel.cpp src/ui/tools-ui.cpp src/ui/native-click.cpp LIBS normaliz)
dofus_test(TreasureHuntTests tests/treasure/treasure-hunt-tests.cpp src/treasure/treasure-hunt.cpp)
dofus_test(TreasureOverlayTests tests/treasure/treasure-overlay-tests.cpp)
add_test(NAME TreasureOverlayRuntimeTests COMMAND TreasureOverlay --self-test)
set_tests_properties(TreasureOverlayRuntimeTests PROPERTIES TIMEOUT 25)

# Notebook and job XP panels.
dofus_test(NotebookDataTests tests/notebook/notebook-data-tests.cpp src/notebook/notebook-data.cpp)
dofus_test(NotebookPanelTests tests/notebook/notebook-panel-tests.cpp src/notebook/notebook-panel.cpp
  src/notebook/notebook-data.cpp src/souls/soul-panel.cpp src/ui/tools-ui.cpp src/ui/native-click.cpp)
dofus_test(NotebookNativeTests tests/notebook/notebook-native-tests.cpp src/notebook/notebook-native.cpp
  src/notebook/notebook-data.cpp LIBS user32)
dofus_test(JobsXpDataTests tests/jobs/jobs-xp-data-tests.cpp src/jobs/jobs-xp-data.cpp src/jobs/jobs-xp-http.cpp LIBS winhttp)
dofus_test(JobsXpPanelTests tests/jobs/jobs-xp-panel-tests.cpp src/jobs/jobs-xp-data.cpp)

# Spouse in the party menu.
dofus_test(SpousePartyMenuTests tests/social/spouse-party-menu-tests.cpp src/social/spouse-party-menu.cpp)
