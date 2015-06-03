
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

void BufferBlockManager::EnqueueBlockForRead(BufferBlock* block)
{
	queueLock.lock();

	block->actualSize = 0;
	block->isLastBlock = false;
	blocksPendingRead.push(block);

	queueLock.unlock();

	signal.notify_one();
}

void BufferBlockManager::DequeueBlockForRead(BufferBlock** block)
{
WAITFOR:

	if (blocksPendingRead.size() == 0)
		WaitForEnqueue();

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

void BufferBlockManager::EnqueueBlockForWrite(BufferBlock* block)
{
	queueLock.lock();

	blocksPendingWrite.push(block);

	queueLock.unlock();

	signal.notify_one();
}

void BufferBlockManager::DequeueBlockForWrite(BufferBlock** block)
{
WAITFOR:

	if (blocksPendingWrite.size() == 0)
		WaitForEnqueue();

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

void BufferBlockManager::ResetState()
{
	queueLock.lock();

	blocksPendingRead = std::queue<BufferBlock*>();
	blocksPendingWrite = std::queue<BufferBlock*>();

	for (std::list<BufferBlock*>::const_iterator iterator = blocks.begin(), end = blocks.end(); iterator != end; ++iterator) {
		(*iterator)->actualSize = 0;
	}

	queueLock.unlock();
}

void BufferBlockManager::WaitForEnqueue()
{
	std::unique_lock<std::mutex> lk(signalLock);
	signal.wait(lk);
}

struct AsyncCopyContext
{
	AsyncCopyContext(BufferBlockManager* bufferBlockManager, ReadStream* readStream, WriteStream* writeStream, AsyncCopyStatus* copyStatus)
	{
		this->bufferBlockManager = bufferBlockManager;
		this->readStream = readStream;
		this->writeStream = writeStream;
		this->readResult = ASYNC_COPY_READ_WRITE_SUCCESS;
		this->writeResult = ASYNC_COPY_READ_WRITE_SUCCESS;
		this->copyStatus = copyStatus;
		this->didFinishWrite = false;
	}
	void BytesWritten(int bytesWritten)
	{
		if (copyStatus)
			copyStatus->BytesWritten(bytesWritten);
	}
	void BytesRead(int numberOfBytesRead)
	{
		if (copyStatus)
			copyStatus->BytesRead(numberOfBytesRead);
	}
	bool Cancel()
	{
		if (copyStatus)
			return copyStatus->IsCancelled();
		return false;
	}
	void ReadThreadInit()
	{
		if (copyStatus)
			return copyStatus->ReadThreadInit();
	}
	void ReadThreadDestroy()
	{
		if (copyStatus)
			return copyStatus->ReadThreadDestroy();
	}
	void WriteThreadInit()
	{
		if (copyStatus)
			return copyStatus->WriteThreadInit();
	}
	void WriteThreadDestroy()
	{
		if (copyStatus)
			return copyStatus->WriteThreadDestroy();
	}
	BufferBlockManager* bufferBlockManager;
	ReadStream* readStream;
	WriteStream* writeStream;
	int readResult;
	int writeResult;
	AsyncCopyStatus* copyStatus;
	bool didFinishWrite;
};

void ReadStreamThread(AsyncCopyContext* asyncContext)
{
	asyncContext->ReadThreadInit();

	int bytesRead = 0;
	BufferBlock* readBuffer = NULL;
	int readResult = ASYNC_COPY_READ_WRITE_SUCCESS;

	while (
		// as long there hasn't been any write errors
		asyncContext->writeResult == ASYNC_COPY_READ_WRITE_SUCCESS
		// and we haven't had an error reading yet
		&& readResult == ASYNC_COPY_READ_WRITE_SUCCESS)
	{
		if (asyncContext->Cancel())
			return;

		// let's deque a block to read to!
		asyncContext->bufferBlockManager->DequeueBlockForRead(&readBuffer);

		readResult = asyncContext->readStream->Read(readBuffer->buffer, readBuffer->bufferSize, &bytesRead);
		readBuffer->actualSize = bytesRead;
		readBuffer->isLastBlock = bytesRead == 0;

		if (readResult == ASYNC_COPY_READ_WRITE_SUCCESS)
		{
			// this was a valid read, go ahead and queue it for writing
			asyncContext->bufferBlockManager->EnqueueBlockForWrite(readBuffer);
			asyncContext->BytesRead(bytesRead);
		}
		else
		{
			// an error occured reading
			asyncContext->readResult = readResult;

			// since an error occured, lets queue an block to write indicatiting we are done and there are no more bytes to read
			readBuffer->isLastBlock = true;
			readBuffer->actualSize = 0;

			asyncContext->bufferBlockManager->EnqueueBlockForWrite(readBuffer);
		}

		if (readBuffer->isLastBlock)
		{
			break;
		}
	}

	asyncContext->ReadThreadDestroy();
}

void WriteStreamThread(AsyncCopyContext* asyncContext)
{
	asyncContext->WriteThreadInit();

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
		if (asyncContext->Cancel())
			return;

		// lets dequeue a block for writing!
		asyncContext->bufferBlockManager->DequeueBlockForWrite(&writeBuffer);

		isLastWriteBlock = writeBuffer->isLastBlock;

		if (writeBuffer->actualSize > 0)
			writeResult = asyncContext->writeStream->Write(writeBuffer->buffer, writeBuffer->actualSize, &bytesWritten);

		if (writeResult == ASYNC_COPY_READ_WRITE_SUCCESS)
		{
			asyncContext->bufferBlockManager->EnqueueBlockForRead(writeBuffer);
			asyncContext->BytesWritten(bytesWritten);
			if (isLastWriteBlock)
			{
				asyncContext->didFinishWrite = true;
				break;
			}
		}
		else
		{
			asyncContext->writeResult = writeResult;
			asyncContext->bufferBlockManager->EnqueueBlockForRead(writeBuffer);
			break;
		}
	}

	asyncContext->WriteThreadDestroy();
}

void AsyncCopyStream(BufferBlockManager* bufferBlockManager, ReadStream* readStream, WriteStream* writeStream, int* readResult, int* writeResult, bool* didFinish, AsyncCopyStatus* copyStatus)
{
	AsyncCopyContext asyncContext(bufferBlockManager, readStream, writeStream, copyStatus);
	std::thread readThread(ReadStreamThread, &asyncContext);
	std::thread writeThread(WriteStreamThread, &asyncContext);

	readThread.join();
	writeThread.join();

	*readResult = asyncContext.readResult;
	*writeResult = asyncContext.writeResult;
	*didFinish = asyncContext.didFinishWrite;
}