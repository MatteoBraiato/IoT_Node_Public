# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS bsp/bsp_i2c.c bdi/bdi_i2c.c bsp/bsp_do.c bsp/bsp_uart.c bsp/bsp_di.c bsp/bsp_bme280.c bsp/bsp_max1704.c         # list the source files of this component
    INCLUDE_DIRS "./" "../lib" "../cfg"         # optional, add here public include directories
    PRIV_INCLUDE_DIRS                           # optional, add here private include directories
    REQUIRES espressif__bme280                  # optional, list the public requirements (component names)
    PRIV_REQUIRES fatfs nvs_flash driver        # optional, list the private requirements
)
