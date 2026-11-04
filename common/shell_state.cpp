#include "shell_state.h"

#include "log.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace shell {

namespace {

const char *kPath = "/tmp/lgtv-shell.state";
constexpr uint32_t kMagic = 0x6c677368;  // "lgsh"
constexpr uint32_t kVersion = 1;

template <typename T>
struct Half {
    uint32_t seq;  // odd while its writer is mid-update
    T data;
};

template <typename T>
T read_half(const Half<T> &h) {
    T out;
    // A writer takes microseconds; one that died mid-update would leave the
    // count odd for good, so give up waiting after a while.
    for (int tries = 0; tries < 10000; tries++) {
        uint32_t before = __atomic_load_n(&h.seq, __ATOMIC_ACQUIRE);
        std::memcpy(static_cast<void *>(&out), &h.data, sizeof out);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (!(before & 1) && __atomic_load_n(&h.seq, __ATOMIC_RELAXED) == before) break;
    }
    return out;
}

template <typename T>
void write_half(Half<T> &h, const T &value) {
    __atomic_store_n(&h.seq, h.seq + 1, __ATOMIC_RELAXED);
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(static_cast<void *>(&h.data), &value, sizeof value);
    __atomic_store_n(&h.seq, h.seq + 1, __ATOMIC_RELEASE);
}

}  // namespace

struct State::File {
    uint32_t magic, version;
    Half<HomeState> home;
    Half<ControlState> control;
};

uint64_t now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1000000;
}

State::~State() {
    if (map_) munmap(map_, sizeof(File));
}

bool State::open() {
    if (map_) return true;
    // The apps run as different users. /tmp is sticky and the TV protects
    // regular files there (fs.protected_regular), so O_CREAT on the other
    // app's file fails: open an existing one without it.
    int fd = ::open(kPath, O_RDWR | O_CLOEXEC);
    bool created = false;
    if (fd < 0 && errno == ENOENT) {
        fd = ::open(kPath, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (fd >= 0) {
            created = true;
            fchmod(fd, 0666);  // past the umask: the other app writes it too
        } else if (errno == EEXIST) {
            fd = ::open(kPath, O_RDWR | O_CLOEXEC);
        }
    }
    if (fd < 0) {
        LOG("shell state: %s: %s", kPath, std::strerror(errno));
        return false;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || (st.st_size < off_t(sizeof(File)) && ftruncate(fd, sizeof(File)) != 0)) {
        LOG("shell state: %s: can't size it: %s", kPath, std::strerror(errno));
        close(fd);
        return false;
    }
    void *p = mmap(nullptr, sizeof(File), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        LOG("shell state: mmap: %s", std::strerror(errno));
        return false;
    }
    map_ = static_cast<File *>(p);
    if (created || map_->magic != kMagic || map_->version != kVersion) {
        // New, or left by another version of the apps: start from defaults.
        write_half(map_->home, HomeState{});
        write_half(map_->control, ControlState{});
        map_->version = kVersion;
        __atomic_store_n(&map_->magic, kMagic, __ATOMIC_RELEASE);
    }
    return true;
}

HomeState State::home() const { return map_ ? read_half(map_->home) : HomeState{}; }

void State::set_home(const HomeState &s) {
    if (map_) write_half(map_->home, s);
}

ControlState State::control() const { return map_ ? read_half(map_->control) : ControlState{}; }

void State::set_control(const ControlState &s) {
    if (map_) write_half(map_->control, s);
}

uint32_t State::home_version() const { return map_ ? __atomic_load_n(&map_->home.seq, __ATOMIC_ACQUIRE) : 0; }

}  // namespace shell
