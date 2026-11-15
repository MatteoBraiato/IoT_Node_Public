# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS bosch_bme280/bme280.c                           # list the source files of this component
    INCLUDE_DIRS "./"  "../cfg" "./bosch_bme280"    # optional, add here public include directories
    PRIV_INCLUDE_DIRS               # optional, add here private include directories
    REQUIRES                        # optional, list the public requirements (component names)
    PRIV_REQUIRES                   # optional, list the private requirements
)
