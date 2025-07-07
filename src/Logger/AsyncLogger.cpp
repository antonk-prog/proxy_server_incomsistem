#include "AsyncLogger.hpp"
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
constexpr size_t INITIAL_FILE_SIZE = 1024 * 1024;

AsyncLogger::AsyncLogger(const std::string& filename, size_t batchSize, int flushIntervalMs)
    : filename(filename), batchSize(batchSize), flushIntervalMs(flushIntervalMs)
{
    fd = open(filename.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("open");
        throw std::runtime_error("Failed to open log file");
    }

    if (ftruncate(fd, INITIAL_FILE_SIZE) != 0) {
        perror("ftruncate");
        throw std::runtime_error("Failed to set file size");
    }

    mappedSize = INITIAL_FILE_SIZE;
    mapped = static_cast<char*>(mmap(nullptr, mappedSize, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0));
    if (mapped == MAP_FAILED) {
        perror("mmap");
        throw std::runtime_error("Failed to mmap file");
    }

    worker = std::thread(&AsyncLogger::process, this);
}

AsyncLogger::~AsyncLogger() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
    }
    cv.notify_all();
    if (worker.joinable())
        worker.join();

    if (mapped != MAP_FAILED && mapped != nullptr)
        munmap(mapped, mappedSize);
    if (fd >= 0)
        close(fd);
}

void AsyncLogger::log(std::string message) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        frontBuffer.push_back(std::move(message)); 

        if (frontBuffer.size() >= batchSize) {
            cv.notify_one();
        }
    }
}

void AsyncLogger::ensureCapacity(size_t requiredSize) {
    const size_t reserveThreshold = mappedSize * 9 / 10;         // 90% граница
    const size_t expansionStep = 128 * 1024 * 1024;               // 128 мб

    if (requiredSize < reserveThreshold)
        return; 

    size_t newSize = mappedSize;
    while (newSize <= requiredSize) {
        newSize += expansionStep;
    }

    if (ftruncate(fd, newSize) != 0) {
        perror("ftruncate failed");
        return;
    }

    if (mapped) {
        msync(mapped, mappedSize, MS_SYNC);
        munmap(mapped, mappedSize);
    }

    mapped = static_cast<char*>(mmap(nullptr, newSize, PROT_WRITE, MAP_SHARED, fd, 0));
    if (mapped == MAP_FAILED) {
        perror("mmap failed");
        mapped = nullptr;
        return;
    }

    mappedSize = newSize;

    madvise(mapped, mappedSize, MADV_SEQUENTIAL);
}

void AsyncLogger::process() {
    size_t syncCounter = 0;
    const size_t syncEveryN = 20; 
    std::vector<std::string> localBuffer;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait_for(lock, std::chrono::milliseconds(flushIntervalMs), [&] {
                return !frontBuffer.empty() || done;
            });

            if (!frontBuffer.empty()) {
                frontBuffer.swap(localBuffer);
            }

            if (done && frontBuffer.empty())
                break;
        }

        if (!localBuffer.empty()) {
            size_t batchSizeBytes = 0;

            for (const auto& msg : localBuffer) {
                size_t len = msg.size();
                ensureCapacity(writeOffset + len + 1); // +1 на '\n'

                std::memcpy(mapped + writeOffset, msg.c_str(), len);
                writeOffset += len;
                mapped[writeOffset++] = '\n';

                batchSizeBytes += len + 1;
            }

            localBuffer.clear();

 
            if (++syncCounter >= syncEveryN) {
                msync(mapped, writeOffset, MS_ASYNC);
                syncCounter = 0;
            }
        }
    }

    std::vector<std::string> remaining;
    {
        std::lock_guard<std::mutex> lock(mutex);
        frontBuffer.swap(remaining);
    }

    for (const auto& msg : remaining) {
        size_t len = msg.size();
        ensureCapacity(writeOffset + len + 1);

        std::memcpy(mapped + writeOffset, msg.c_str(), len);
        writeOffset += len;
        mapped[writeOffset++] = '\n';
    }

    if (writeOffset > 0) {
        msync(mapped, writeOffset, MS_SYNC); 
    }
}