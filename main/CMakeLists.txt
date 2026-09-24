# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS 
    	"main.c" 
    	"wifi/wifi_manager.c" 
    	"wifi/wifi_manager_test.c" 
    	"time/sntp_time_manager.c"
    INCLUDE_DIRS         
        "."
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES            # optional, list the public requirements (component names)
    PRIV_REQUIRES       # optional, list the private requirements
    EMBED_TXTFILES 
)
 