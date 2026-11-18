#include "background.hpp"

#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/resource.h>
#    include <unistd.h>
#    ifdef __linux__
#        include <sys/syscall.h>
#    endif
#    ifdef __APPLE__
#        include <pthread.h>
#        include <sys/qos.h>
#    endif
#endif

namespace fcg::study {

namespace {
thread_local bool g_background = false;
}

void enterBackground() noexcept {
    g_background = true;
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#elif defined(__linux__)
    // Linux gives each thread a niceness of its own.
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 19);
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
}

bool inBackground() noexcept { return g_background; }

bool loweredBySystem() noexcept {
#ifdef _WIN32
    return GetThreadPriority(GetCurrentThread()) == THREAD_PRIORITY_LOWEST;
#elif defined(__linux__)
    return getpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid))) == 19;
#elif defined(__APPLE__)
    qos_class_t qos = QOS_CLASS_UNSPECIFIED;
    int relative    = 0;
    pthread_get_qos_class_np(pthread_self(), &qos, &relative);
    return qos == QOS_CLASS_UTILITY;
#else
    return g_background;
#endif
}

}  // namespace fcg::study
