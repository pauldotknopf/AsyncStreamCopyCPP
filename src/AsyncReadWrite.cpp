
#include "AsyncReadWrite.h"

struct BufferBlock
{
	BufferBlock(int bufferSize) : buffer(NULL)
	{
		this->bufferSize = bufferSize;
		this->buffer = new char[bufferSize];
		this->actualSize = 0;
		this->isLastBlock = false;
	}
	~BufferBlock()
	{
		this->bufferSize = 0;
		free(this->buffer);
		this->buffer = NULL;
		this->actualSize = 0;
	}
	char* buffer;
	int bufferSize;
	int actualSize;
	bool isLastBlock;
};

BufferBlockManager::BufferBlockManager(int numberOfBlocks, int bufferSize)
{
	dequeueSleepTime = std::chrono::milliseconds(100);
	for (int x = 0; x < numberOfBlocks; x++)
	{
		BufferBlock* block = new BufferBlock(bufferSize);
		blocks.push_front(block);
		blocksPendingRead.push(block);
	}
}

BufferBlockManager::~BufferBlockManager()
{
	for (std::list<BufferBlock*>::const_iterator iterator = blocks.begin(), end = blocks.end(); iterator != end; ++iterator) {
		delete (*iterator);
	}
}

void BufferBlockManager::enqueueBlockForRead(BufferBlock* block)
{
	queueLock.lock();

	block->actualSize = 0;
	block->isLastBlock = false;
	blocksPendingRead.push(block);

	queueLock.unlock();
}

void BufferBlockManager::dequeueBlockForRead(BufferBlock** block)
{
WAITFOR:
	while (blocksPendingRead.size() == 0)
		std::this_thread::sleep_for(dequeueSleepTime);

	queueLock.lock();

	if (blocksPendingRead.size() == 0)
	{
		queueLock.unlock();
		goto WAITFOR;
	}

	*block = blocksPendingRead.front();

	blocksPendingRead.pop();

	queueLock.unlock();
}

void BufferBlockManager::enqueueBlockForWrite(BufferBlock* block)
{
	queueLock.lock();

	blocksPendingWrite.push(block);

	queueLock.unlock();
}

void BufferBlockManager::dequeueBlockForWrite(BufferBlock** block)
{
WAITFOR:
	while (blocksPendingWrite.size() == 0)
		std::this_thread::sleep_for(dequeueSleepTime);

	queueLock.lock();

	if (blocksPendingWrite.size() == 0)
	{
		queueLock.unlock();
		goto WAITFOR;
	}

	*block = blocksPendingWrite.front();

	blocksPendingWrite.pop();

	queueLock.unlock();
}

void BufferBlockManager::resetState()
{
	queueLock.lock();

	blocksPendingRead = std::queue<BufferBlock*>();
	blocksPendingWrite = std::queue<BufferBlock*>();

	for (std::list<BufferBlock*>::const_iterator iterator = blocks.begin(), end = blocks.end(); iterator != end; ++iterator) {
		(*iterator)->actualSize = 0;
	}

	queueLock.unlock();
}

struct AsyncCopyContext
{
	AsyncCopyContext(BufferBlockManager* bufferBlockManager, ReadStream* readStream, WriteStream* writeStream)
	{
		this->bufferBlockManager = bufferBlockManager;
		this->readStream = readStream;
		this->writeStream = writeStream;
		this->readResult = ASYNC_COPY_READ_WRITE_SUCCESS;
		this->writeResult = ASYNC_COPY_READ_WRITE_SUCCESS;
	}
	BufferBlockManager* bufferBlockManager;
	ReadStream* readStream;
	WriteStream* writeStream;
	int readResult;
	int writeResult;
};

void ReadStreamThread(AsyncCopyContext* asyncContext)
{
	int bytesRead = 0;
	BufferBlock* readBuffer = NULL;
	int readResult = ASYNC_COPY_READ_WRITE_SUCCESS;

	while (
		// as long there hasn't been any write errors
		asyncContext->writeResult == ASYNC_COPY_READ_WRITE_SUCCESS
		// and we haven't had an error reading yet
		&& readResult == ASYNC_COPY_READ_WRITE_SUCCESS)
	{
		// let's deque a block to read to!
		asyncContext->bufferBlockManager->dequeueBlockForRead(&readBuffer);

		readResult = asyncContext->readStream->read(readBuffer->buffer, readBuffer->bufferSize, &bytesRead);
		readBuffer->actualSize = bytesRead;
		readBuffer->isLastBlock = bytesRead == 0;

		if (readResult == ASYNC_COPY_READ_WRITE_SUCCESS)
		{
			// this was a valid read, go ahead and queue it for writing
			asyncContext->bufferBlockManager->enqueueBlockForWrite(readBuffer);
		}
		else
		{
			// an error occured reading
			asyncContext->readResult = readResult;

			// since an error occured, lets queue an block to write indicatiting we are done and there are no more bytes to read
			readBuffer->isLastBlock = true;
			readBuffer->actualSize = 0;

			asyncContext->bufferBlockManager->enqueueBlockForWrite(readBuffer);
		}

		if (readBuffer->isLastBlock) return;
	}
}

void WriteStreamThread(AsyncCopyContext* asyncContext)
{
	int bytesWritten = 0;
	BufferBlock* writeBuffer = NULL;
	int writeResult = ASYNC_COPY_READ_WRITE_SUCCESS;
	bool isLastWriteBlock = false;

	while (
		// as long as there are no errors during reading
		asyncContext->readResult == ASYNC_COPY_READ_WRITE_SUCCESS
		// and we haven't had an error writing yet
		&& writeResult == ASYNC_COPY_READ_WRITE_SUCCESS)
	{
		// lets dequeue a block for writing!
		asyncContext->bufferBlockManager->dequeueBlockForWrite(&writeBuffer);

		isLastWriteBlock = writeBuffer->isLastBlock;

		if (writeBuffer->actualSize > 0)
			writeResult = asyncContext->writeStream->write(writeBuffer->buffer, writeBuffer->actualSize, &bytesWritten);

		if (writeResult == ASYNC_COPY_READ_WRITE_SUCCESS)
		{
			asyncContext->bufferBlockManager->enqueueBlockForRead(writeBuffer);
			if (isLastWriteBlock) return;
		}
		else
		{
			asyncContext->writeResult = writeResult;
			asyncContext->bufferBlockManager->enqueueBlockForRead(writeBuffer);
			return;
		}
	}
}

void AsyncCopyStream(BufferBlockManager* bufferBlockManager, ReadStream* readStream, WriteStream* writeStream, int* readResult, int* writeResult)
{
	AsyncCopyContext asyncContext(bufferBlockManager, readStream, writeStream);
	std::thread readThread(ReadStreamThread, &asyncContext);
	std::thread writeThread(WriteStreamThread, &asyncContext);

	readThread.join();
	writeThread.join();

	*readResult = asyncContext.readResult;
	*writeResult = asyncContext.writeResult;
}