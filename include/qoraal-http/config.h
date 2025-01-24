

/* CFG_LITTLEFS_DISABLE
    If defined, littlefs filesystem will be disabled.
*/
#define CFG_LITTLEFS_DISABLE    1




#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#elif __has_include(<lwip/inet.h>) || (defined QORAAL_CFG_USE_LWIP && QORAAL_CFG_USE_LWIP)
// Looks like LWIP is present
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#ifdef QORAAL_CFG_USE_LWIP
#undef QORAAL_CFG_USE_LWIP
#endif
#define QORAAL_CFG_USE_LWIP            1
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>
    #define closesocket close
#endif
