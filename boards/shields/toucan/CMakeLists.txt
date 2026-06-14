# boards/shields/toucan/CMakeLists.txt

# シールド用の独立した空間ではなく、ZMKのコアシステム（app）に直接ソースコードを結合させます
if(CONFIG_ZMK_INPUT_PROCESSOR_TOUCAN_CIRCLE)
    target_sources(app PRIVATE input_processor_toucan_circle.c)
endif()
