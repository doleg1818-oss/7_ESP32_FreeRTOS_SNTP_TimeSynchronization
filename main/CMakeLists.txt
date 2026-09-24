# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS 
    	"app/main.c" 
    	"wifi/wifi_manager.c" 
    	"wifi/wifi_manager_test.c" 
    	"time/sntp_time_manager.c"
    	"http/http_client.c"
    	"http/http_client_task.c" 
    	"http/http_manager.h"
    	"http/http_manager.c"
    	"http/http_recovery_manager.c"
    	"http/http_recovery_task.c"
    	"data/server_data_parser.c"
    	"data/device_data_serializer.c" 
    	"app/telemetry_task.c" 
    	"app/http_result_task.c" 
    INCLUDE_DIRS         
        "."
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES            # optional, list the public requirements (component names)
    PRIV_REQUIRES       # optional, list the private requirements
    EMBED_TXTFILES "certs/server_cert.pem"
)
 