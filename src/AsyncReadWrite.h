#pragma once

#include <stdlib.h>
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>
#include <list>
#include <thread>
#include <condition_variable>

#define ASYNC_COPY_READ_WRITE_SUCCESS 0

struct BufferBlock;

struct ReadStream
{
	// read a stream to a buffer.
	// return non-zero if error occured
	virtual int Read(char* buffer, int bufferSize, int* bytesRead) = 0;
};

struct WriteStream
{
	// write a buffer to a stream.
	// return non-zero if error occured
	virtual int Write(char* buffer, int bufferSize, int* bytesWritten) = 0;
};

struct AsyncCopyStatus
{
	virtual void BytesWritten(int bytesWritten) { };
	virtual void BytesRead(int numberOfBytesRead) { };
	virtual bool IsCancelled() { return false; }
	virtual void ReadThreadInit() { }
	virtual void ReadThreadDestroy() { }
	virtual void WriteThreadInit() { }
	virtual void WriteThreadDestroy() { }
};

class BufferBlockManager
{

public:

	BufferBlockManager(int numberOfBlocks, int bufferSize);
	~BufferBlockManager();

	void EnqueueBlockForRead(BufferBlock* block);
	void DequeueBlockForRead(BufferBlock** block);
	void EnqueueBlockForWrite(BufferBlock* block);
	void DequeueBlockForWrite(BufferBlock** block);
	void ResetState();

private:

	void WaitForEnqueue();

	std::list<BufferBlock*> blocks;
	std::queue<BufferBlock*> blocksPendingRead;
	std::queue<BufferBlock*> blocksPendingWrite;
	std::mutex queueLock;
	std::mutex signalLock;
	std::condition_variable signal;
};

void AsyncCopyStream(BufferBlockManager* bufferBlockManager, ReadStream* readStream, WriteStream* writeStream, int* readResult, int* writeResult, bool* didFinish, AsyncCopyStatus* copyStatus);