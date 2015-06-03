#pragma once

#include <stdlib.h>
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>
#include <list>
#include <thread>

#define ASYNC_COPY_READ_WRITE_SUCCESS 0

struct BufferBlock;

struct ReadStream
{
	// read a stream to a buffer.
	// return non-zero if error occured
	virtual int read(char* buffer, int bufferSize, int* bytesRead) = 0;
};

struct WriteStream
{
	// write a buffer to a stream.
	// return non-zero if error occured
	virtual int write(char* buffer, int bufferSize, int* bytesWritten) = 0;
};

class BufferBlockManager
{

public:

	BufferBlockManager(int numberOfBlocks, int bufferSize);
	~BufferBlockManager();

	void enqueueBlockForRead(BufferBlock* block);
	void dequeueBlockForRead(BufferBlock** block);
	void enqueueBlockForWrite(BufferBlock* block);
	void dequeueBlockForWrite(BufferBlock** block);
	void resetState();

private:

	std::list<BufferBlock*> blocks;
	std::queue<BufferBlock*> blocksPendingRead;
	std::queue<BufferBlock*> blocksPendingWrite;
	std::mutex queueLock;
	std::chrono::milliseconds dequeueSleepTime;

};

void AsyncCopyStream(BufferBlockManager* bufferBlockManager, ReadStream* readStream, WriteStream* writeStream, int* readResult, int* writeResult);