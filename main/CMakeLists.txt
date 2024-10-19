idf_component_register(
    SRCS
        "main.c"
        "bsp_lvgl.c"
    INCLUDE_DIRS
        "."
)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
idf_component_get_property(freetype_lib espressif__freetype COMPONENT_LIB)
target_link_libraries(${lvgl_lib} PUBLIC ${freetype_lib})

#fatfs_create_spiflash_image(fat fat FLASH_IN_PROJECT)
