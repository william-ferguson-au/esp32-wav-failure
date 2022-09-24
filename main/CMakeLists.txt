set (COMPONENT_SRCS
        "src/main.cpp"
        )

register_component()

# Bundle the files from the spiffs_data folder into the spiffs partition
spiffs_create_partition_image(spiffs_partition spiffs_data FLASH_IN_PROJECT)

