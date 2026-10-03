#include <core/Log.hpp>

int main() {
    ursa::LogSession log;
    if (!log.status())
        return 1;
    URSA_LOG_INFO("Example", "Logging ready");
    return 0;
}
