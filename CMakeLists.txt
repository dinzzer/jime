cmake_minimum_required(VERSION 3.16)
project(jime CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /W3)
    add_compile_definitions(UNICODE _UNICODE)
endif()

# ---- 変換エンジン (プラットフォーム非依存) ----
add_library(jime_engine STATIC
    src/engine/engine.cpp
)
target_include_directories(jime_engine PUBLIC src/engine)

# ---- CLI テストハーネス ----
add_executable(jime_test
    src/engine/test_main.cpp
)
target_link_libraries(jime_test PRIVATE jime_engine)

# ---- TSF テキストサービス DLL (Windows のみ) ----
if(WIN32)
    add_library(jime SHARED
        src/tsf/TextService.cpp
        src/tsf/DllServer.cpp
        src/tsf/jime.def
    )
    target_link_libraries(jime PRIVATE jime_engine user32 gdi32 ole32 oleaut32 advapi32 shell32 dwmapi gdiplus uuid)
    set_target_properties(jime PROPERTIES PREFIX "")

    # 設定コンソール
    add_executable(jime_config WIN32
        src/config/jime_config.cpp
    )
    target_link_libraries(jime_config PRIVATE user32 gdi32 advapi32 comdlg32 dwmapi)
endif()
