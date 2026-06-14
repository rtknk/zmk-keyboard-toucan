# config/CMakeLists.txt

# 最上流の設定として、ZMKコアシステム(app)へ強制的にC言語ファイルを合流させます
if(CONFIG_ZMK_INPUT_PROCESSOR_TOUCAN_CIRCLE)
    target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/input_processor_toucan_circle.c)
endif()
