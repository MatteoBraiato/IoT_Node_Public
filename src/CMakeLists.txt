# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS app.c e5lora.c                         # list the source files of this component
    INCLUDE_DIRS "../lib" "../system" "../cfg"  # optional, add here public include directories
    PRIV_INCLUDE_DIRS                           # optional, add here private include directories
    REQUIRES                                    # optional, list the public requirements (component names)
    PRIV_REQUIRES driver                              # optional, list the private requirements
)
