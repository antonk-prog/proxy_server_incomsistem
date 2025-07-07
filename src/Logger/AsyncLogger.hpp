#pragma once
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AsyncLogger {
  public:
	AsyncLogger(const std::string &filename, size_t batchSize = 10'000,
				int flushIntervalMs = 10);
	~AsyncLogger();

	void log(std::string essage);

  private:
	void process();
	void ensureCapacity(size_t bytesNeeded);

	std::string filename;
	size_t batchSize;
	int flushIntervalMs;

	std::vector<std::string> frontBuffer;
	std::vector<std::string> backBuffer;

	std::mutex mutex;
	std::condition_variable cv;
	bool done = false;
	std::thread worker;

	int fd = -1;
	char *mapped = nullptr;
	size_t mappedSize = 0;
	size_t writeOffset = 0;
};
